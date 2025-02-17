#include "backends/p4tools/modules/testgen/targets/bmv2/test_backend.h"

#include <stdlib.h>

#include <map>
#include <optional>
#include <string>
#include <utility>

#include <boost/multiprecision/cpp_int.hpp>

#include "backends/p4tools/common/lib/model.h"
#include "backends/p4tools/common/lib/trace_event.h"
#include "backends/p4tools/common/lib/util.h"
#include "ir/ir.h"
#include "ir/irutils.h"
#include "lib/cstring.h"
#include "lib/error.h"
#include "lib/exceptions.h"
#include "lib/log.h"

#include "backends/p4tools/modules/testgen/core/program_info.h"
#include "backends/p4tools/modules/testgen/core/symbolic_executor/symbolic_executor.h"
#include "backends/p4tools/modules/testgen/lib/execution_state.h"
#include "backends/p4tools/modules/testgen/lib/test_backend.h"
#include "backends/p4tools/modules/testgen/options.h"
#include "backends/p4tools/modules/testgen/targets/bmv2/backend/metadata/metadata.h"
#include "backends/p4tools/modules/testgen/targets/bmv2/backend/protobuf/protobuf.h"
#include "backends/p4tools/modules/testgen/targets/bmv2/backend/ptf/ptf.h"
#include "backends/p4tools/modules/testgen/targets/bmv2/backend/stf/stf.h"
#include "backends/p4tools/modules/testgen/targets/bmv2/program_info.h"
#include "backends/p4tools/modules/testgen/targets/bmv2/test_spec.h"

namespace P4Tools::P4Testgen::Bmv2 {

const big_int Bmv2TestBackend::ZERO_PKT_VAL = 0x2000000;
const big_int Bmv2TestBackend::ZERO_PKT_MAX = 0xffffffff;
const std::set<std::string> Bmv2TestBackend::SUPPORTED_BACKENDS = {"PTF", "STF", "PROTOBUF",
                                                                   "METADATA"};

Bmv2TestBackend::Bmv2TestBackend(const ProgramInfo &programInfo, SymbolicExecutor &symbex,
                                 const std::filesystem::path &testPath,
                                 std::optional<uint32_t> seed)
    : TestBackEnd(programInfo, symbex) {
    cstring testBackendString = TestgenOptions::get().testBackend;
    if (testBackendString.isNullOrEmpty()) {
        ::error(
            "No test back end provided. Please provide a test back end using the --test-backend "
            "parameter.");
        exit(EXIT_FAILURE);
    }

    if (testBackendString == "PTF") {
        testWriter = new PTF(testPath, seed);
    } else if (testBackendString == "STF") {
        testWriter = new STF(testPath, seed);
    } else if (testBackendString == "PROTOBUF") {
        testWriter = new Protobuf(testPath, seed);
    } else if (testBackendString == "METADATA") {
        testWriter = new Metadata(testPath, seed);
    } else {
        P4C_UNIMPLEMENTED(
            "Test back end %1% not implemented for this target. Supported back ends are %2%.",
            testBackendString, Utils::containerToString(SUPPORTED_BACKENDS));
    }
}

TestBackEnd::TestInfo Bmv2TestBackend::produceTestInfo(
    const ExecutionState *executionState, const Model *completedModel,
    const IR::Expression *outputPacketExpr, const IR::Expression *outputPortExpr,
    const std::vector<std::reference_wrapper<const TraceEvent>> *programTraces) {
    auto testInfo = TestBackEnd::produceTestInfo(executionState, completedModel, outputPacketExpr,
                                                 outputPortExpr, programTraces);
    // This is a hack to deal with a behavioral model quirk.
    // Packets that are too small are truncated to 02000000 (in hex) with width 32 bit.
    if (testInfo.outputPacket->type->width_bits() == 0) {
        int outPktSize = ZERO_PKT_WIDTH;
        testInfo.outputPacket =
            IR::getConstant(IR::getBitType(outPktSize), Bmv2TestBackend::ZERO_PKT_VAL);
        testInfo.packetTaintMask =
            IR::getConstant(IR::getBitType(outPktSize), Bmv2TestBackend::ZERO_PKT_MAX);
    }
    return testInfo;
}

const TestSpec *Bmv2TestBackend::createTestSpec(const ExecutionState *executionState,
                                                const Model *completedModel,
                                                const TestInfo &testInfo) {
    // Create a testSpec.
    TestSpec *testSpec = nullptr;

    const auto *ingressPayload = testInfo.inputPacket;
    const auto *ingressPayloadMask = IR::getConstant(IR::getBitType(1), 1);
    const auto ingressPacket = Packet(testInfo.inputPort, ingressPayload, ingressPayloadMask);

    std::optional<Packet> egressPacket = std::nullopt;
    if (!testInfo.packetIsDropped) {
        egressPacket = Packet(testInfo.outputPort, testInfo.outputPacket, testInfo.packetTaintMask);
    }
    testSpec = new TestSpec(ingressPacket, egressPacket, testInfo.programTraces);

    // If metadata mode is enabled, gather the user metadata variable form the parser.
    // Save the values of all the fields in it and return.
    if (TestgenOptions::get().testBackend == "METADATA") {
        auto *metadataCollection = new MetadataCollection();
        const auto *bmv2ProgInfo = programInfo.checkedTo<BMv2_V1ModelProgramInfo>();
        const auto *localMetadataVar = bmv2ProgInfo->getBlockParam("Parser", 2);
        const auto *localMetadataType = executionState->resolveType(localMetadataVar->type);
        auto flatFields = executionState->getFlatFields(
            localMetadataVar, localMetadataType->checkedTo<IR::Type_Struct>(), {});
        for (const auto *fieldRef : flatFields) {
            const auto *fieldVal = completedModel->evaluate(executionState->get(fieldRef));
            // Try to remove the leading internal name for the metadata field.
            // Thankfully, this string manipulation is safe if we are out of range.
            auto fieldString = fieldRef->toString();
            fieldString = fieldString.substr(fieldString.find('.') - fieldString.begin() + 1);
            metadataCollection->addMetaDataField(fieldString, fieldVal);
        }
        testSpec->addTestObject("metadata_collection", "metadata_collection", metadataCollection);
        return testSpec;
    }

    // We retrieve the individual table configurations from the execution state.
    const auto uninterpretedTableConfigs = executionState->getTestObjectCategory("tableconfigs");
    // Since these configurations are uninterpreted we need to convert them. We launch a
    // helper function to solve the variables involved in each table configuration.
    for (const auto &tablePair : uninterpretedTableConfigs) {
        const auto tableName = tablePair.first;
        const auto *uninterpretedTableConfig = tablePair.second->checkedTo<TableConfig>();
        const auto *const tableConfig = uninterpretedTableConfig->evaluate(*completedModel);
        testSpec->addTestObject("tables", tableName, tableConfig);
    }

    const auto actionProfiles = executionState->getTestObjectCategory("action_profile");
    for (const auto &testObject : actionProfiles) {
        const auto profileName = testObject.first;
        const auto *actionProfile = testObject.second->checkedTo<Bmv2_V1ModelActionProfile>();
        const auto *evaluatedProfile = actionProfile->evaluate(*completedModel);
        testSpec->addTestObject("action_profiles", profileName, evaluatedProfile);
    }

    const auto actionSelectors = executionState->getTestObjectCategory("action_selector");
    for (const auto &testObject : actionSelectors) {
        const auto selectorName = testObject.first;
        const auto *actionSelector = testObject.second->checkedTo<Bmv2_V1ModelActionSelector>();
        const auto *evaluatedSelector = actionSelector->evaluate(*completedModel);
        testSpec->addTestObject("action_selectors", selectorName, evaluatedSelector);
    }

    const auto cloneInfos = executionState->getTestObjectCategory("clone_infos");
    for (const auto &testObject : cloneInfos) {
        const auto sessionId = testObject.first;
        const auto *cloneInfo = testObject.second->checkedTo<Bmv2_CloneInfo>();
        const auto *evaluatedInfo = cloneInfo->evaluate(*completedModel);
        testSpec->addTestObject("clone_infos", sessionId, evaluatedInfo);
    }

    return testSpec;
}

}  // namespace P4Tools::P4Testgen::Bmv2
