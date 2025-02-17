#ifndef BACKENDS_P4TOOLS_MODULES_TESTGEN_CORE_PROGRAM_INFO_H_
#define BACKENDS_P4TOOLS_MODULES_TESTGEN_CORE_PROGRAM_INFO_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "backends/p4tools/common/compiler/reachability.h"
#include "backends/p4tools/common/lib/formulae.h"
#include "ir/declaration.h"
#include "ir/ir.h"
#include "lib/castable.h"
#include "midend/coverage.h"

#include "backends/p4tools/modules/testgen/core/arch_spec.h"
#include "backends/p4tools/modules/testgen/lib/concolic.h"
#include "backends/p4tools/modules/testgen/lib/continuation.h"
#include "backends/p4tools/modules/testgen/lib/namespace_context.h"

namespace P4Tools::P4Testgen {

/// Stores target-specific information about a P4 program.
class ProgramInfo : public ICastable {
 private:
    const NamespaceContext *globalNameSpaceContext;

 protected:
    explicit ProgramInfo(const IR::P4Program *program);

    /// The list of concolic methods implemented by the target. This list is assembled during
    /// initialization.
    ConcolicMethodImpls concolicMethodImpls;

    /// Set of all statements in the input P4 program.
    P4::Coverage::CoverageSet allStatements;

    std::vector<Continuation::Command> pipelineSequence;

    std::optional<const Constraint *> targetConstraints = std::nullopt;

 public:
    ProgramInfo(const ProgramInfo &) = default;

    ProgramInfo(ProgramInfo &&) = default;

    ProgramInfo &operator=(const ProgramInfo &) = default;

    ProgramInfo &operator=(ProgramInfo &&) = default;

    virtual ~ProgramInfo() = default;

    /// The P4 program from which this object is derived.
    const IR::P4Program *program;

    /// The generated dcg.
    const NodesCallGraph *dcg;

    /// @returns the series of nodes that has been computed by this particular target.
    const std::vector<Continuation::Command> *getPipelineSequence() const;

    /// @returns the constraints of this target.
    /// These constraints can influence the execution of the interpreter
    std::optional<const Constraint *> getTargetConstraints() const;

    /// @returns the metadata member corresponding to the ingress port
    virtual const IR::Member *getTargetInputPortVar() const = 0;

    /// @returns the metadata member corresponding to the final output port
    virtual const IR::Member *getTargetOutputPortVar() const = 0;

    /// @returns an expression that checks whether the packet is to be dropped.
    /// The computation is target specific.
    virtual const IR::Expression *dropIsActive() const = 0;

    /// @returns the default value for uninitialized variables for this particular target. This can
    /// be a taint variable or simply 0 (bits) or false (booleans).
    /// If @param forceTaint is active, this function always returns a taint variable.
    virtual const IR::Expression *createTargetUninitialized(const IR::Type *type,
                                                            bool forceTaint) const = 0;

    /// Getter to access allStatements.
    const P4::Coverage::CoverageSet &getAllStatements() const;

    /// @returns the list of implemented concolic methods for this particular program.
    const ConcolicMethodImpls *getConcolicMethodImpls() const;

    // @returns the width of the parser error for this specific target.
    virtual const IR::Type_Bits *getParserErrorType() const = 0;

    /// Looks up a declaration from a path. A BUG occurs if no declaration is found.
    const IR::IDeclaration *findProgramDecl(const IR::Path *path) const;

    /// Looks up a declaration from a path expression. A BUG occurs if no declaration is found.
    const IR::IDeclaration *findProgramDecl(const IR::PathExpression *pathExpr) const;

    /// Resolves a Type_Name in the current environment.
    const IR::Type_Declaration *resolveProgramType(const IR::Type_Name *type) const;

    /// Helper function to produce copy-in and copy-out helper calls.
    /// Copy-in and copy-out is needed to correctly model the value changes of data when it is
    /// copied in and out of a programmable block. In many cases, data is reset here or not even
    /// copied.
    /// TODO: Find a more efficient way to implement copy-in/copy-out. These functions are very
    /// expensive.
    void produceCopyInOutCall(const IR::Parameter *param, size_t paramIdx,
                              const ArchSpec::ArchMember *archMember,
                              std::vector<Continuation::Command> *copyIns,
                              std::vector<Continuation::Command> *copyOuts) const;
};

}  // namespace P4Tools::P4Testgen

#endif /* BACKENDS_P4TOOLS_MODULES_TESTGEN_CORE_PROGRAM_INFO_H_ */
