#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/detail/et_ops.hpp>
#include <boost/multiprecision/number.hpp>
#include <boost/multiprecision/traits/explicit_conversion.hpp>

#include "backends/p4tools/common/core/z3_solver.h"
#include "backends/p4tools/common/lib/formulae.h"
#include "backends/p4tools/common/lib/model.h"
#include "ir/declaration.h"
#include "ir/indexed_vector.h"
#include "ir/ir.h"
#include "lib/big_int_util.h"
#include "lib/cstring.h"
#include "lib/enumerator.h"
#include "lib/exceptions.h"
#include "lib/log.h"
#include "test/gtest/helpers.h"

#include "backends/p4tools/modules/testgen/core/target.h"
#include "backends/p4tools/modules/testgen/test/gtest_utils.h"
#include "backends/p4tools/modules/testgen/test/z3-solver/accessor.h"

namespace Test {

using P4Tools::Model;
using P4Tools::StateVariable;
using P4Tools::Z3Solver;
using P4Tools::Z3SolverAccessor;
using P4Tools::P4Testgen::TestgenTarget;
using Value = IR::Literal;

class Z3SolverTest : public P4ToolsTest {
 protected:
    void SetUp() override {
        opLss = nullptr;
        auto source = P4_SOURCE(P4Headers::V1MODEL, R"(
      header H {
        bit<4> a;
        bit<4> b;
      }

      struct Headers {
        H h;
      }

      struct Metadata { }

      parser parse(packet_in pkt,
                  out Headers hdr,
                  inout Metadata metadata,
                  inout standard_metadata_t sm) {
        state start {
            pkt.extract(hdr.h);
            transition accept;
        }
      }

      control mau(inout Headers hdr, inout Metadata meta, inout standard_metadata_t sm) {
        apply {
          if (hdr.h.a + 4w15 < hdr.h.b)
            hdr.h.b = 1;
        }
      }

      control deparse(packet_out pkt, in Headers hdr) {
        apply {
          pkt.emit(hdr.h);
        }
      }

      control verifyChecksum(inout Headers hdr, inout Metadata meta) {
        apply {}
      }

      control computeChecksum(inout Headers hdr, inout Metadata meta) {
        apply {}
      }

      V1Switch(parse(), verifyChecksum(), mau(), mau(), computeChecksum(), deparse()) main;)");

        const auto test = P4ToolsTestCase::create_16("bmv2", "v1model", source);

        if (!test) {
            return;
        }

        // Produce a ProgramInfo, which is needed to create a SmallStepEvaluator.
        const auto *progInfo = TestgenTarget::initProgram(test->program);
        if (progInfo == nullptr) {
            return;
        }

        // Extract the binary operation from the P4Program
        auto *const declVector = test->program->getDeclsByName("mau")->toVector();
        const auto *decl = (*declVector)[0];
        const auto *control = decl->to<IR::P4Control>();
        for (const auto *st : control->body->components) {
            if (const auto *as = st->to<IR::IfStatement>()) {
                const auto *asExpr = as->condition;
                if (asExpr->is<IR::Lss>()) {
                    opLss = asExpr->to<IR::Lss>();
                }
            }
        }
    }
    const IR::Lss *opLss{};
};

namespace {

void getNumeric(const Value *value, big_int &intValue, bool &flag) {
    if (const auto *constant = value->to<IR::Constant>()) {
        intValue = constant->value;
    } else if (const auto *boolLiteral = value->to<IR::BoolLiteral>()) {
        intValue = big_int(boolLiteral->value);
    } else {
        flag = true;
    }
}

/// Test the step function for v + e binary operation
TEST_F(Z3SolverTest, Assertion2Model) {
    ASSERT_TRUE(opLss);

    // adding asertion
    Z3Solver solver;
    Z3SolverAccessor solverAccessor(solver);

    std::vector<const P4Tools::Constraint *> asserts;
    asserts.push_back(opLss);

    // getting right variable
    ASSERT_TRUE(opLss->right->is<IR::Member>());
    const StateVariable varB = opLss->right->to<IR::Member>();

    // getting numeric and left variable
    ASSERT_TRUE(opLss->left->is<IR::Add>());
    const auto *opAdd = opLss->left->to<IR::Add>();
    ASSERT_TRUE(opAdd->left->is<IR::Member>());
    const StateVariable varA = opAdd->left->to<IR::Member>();
    ASSERT_TRUE(opAdd->right->is<IR::Constant>());
    const auto *addToA = opAdd->right->to<IR::Constant>();

    // getting model without check satisfiable
    EXPECT_THROW(solver.getModel(), Util::CompilerBug);

    // checking satisfiability
    ASSERT_EQ(solver.checkSat(asserts), true);
    Model model2 = *solver.getModel();
    ASSERT_EQ(model2.size(), 2u);

    // checking variables
    ASSERT_GT(model2.count(varA), 0u);
    ASSERT_GT(model2.count(varB), 0u);
    const auto *valueA = model2.at(varA)->to<IR::Literal>();
    const auto *valueB = model2.at(varB)->to<IR::Literal>();

    // input expression was hdr.h.a + 4w15 < hdr.h.b
    // lets calculate it: valueA + addToA < valueB
    big_int intA;
    big_int intAddToA;
    big_int intB;
    bool flag = false;
    getNumeric(valueA, intA, flag);
    getNumeric(addToA, intAddToA, flag);
    getNumeric(valueB, intB, flag);
    ASSERT_FALSE(flag);

    ASSERT_TRUE((intA + intAddToA) % 16 < intB);

    // try to add the same assertion
    asserts.push_back(opLss);

    // try to get model, should have two assertions now
    Model model3 = *solver.getModel();
    ASSERT_EQ(model3.size(), 2u);

    // checking satisfiability
    ASSERT_EQ(solver.checkSat(asserts), true);
    Model model4 = *solver.getModel();
    ASSERT_EQ(model4.size(), 2u);

    // checking variables
    ASSERT_GT(model4.count(varA), 0u);
    ASSERT_GT(model4.count(varB), 0u);
    model4.at(varA)->checkedTo<IR::Literal>();
    model4.at(varB)->checkedTo<IR::Literal>();

    // input expression was hdr.h.a + 4w15 < hdr.h.b
    // lets calculate it: valueA + addToA < valueB
    big_int intA1;
    big_int intB1;
    flag = false;
    getNumeric(valueA, intA1, flag);
    getNumeric(addToA, intAddToA, flag);
    getNumeric(valueB, intB1, flag);
    ASSERT_FALSE(flag);

    ASSERT_TRUE((intA1 + intAddToA) % 16 < intB1);
}

}  // anonymous namespace

}  // namespace Test
