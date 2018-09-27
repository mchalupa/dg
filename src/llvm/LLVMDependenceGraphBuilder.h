#ifndef _DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_
#define _DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_

#include <string>

#include "llvm/LLVMDependenceGraph.h"
#include "llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h"
#include "llvm/analysis/ReachingDefinitions/LLVMReachingDefinitionsAnalysisOptions.h"

#include "llvm/analysis/DefUse.h"
#include "llvm/analysis/PointsTo/PointsTo.h"
#include "analysis/ReachingDefinitions/SemisparseRda.h"
#include "llvm/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "analysis/PointsTo/PointsToFlowInsensitive.h"
#include "analysis/PointsTo/PointsToFlowSensitive.h"
#include "analysis/PointsTo/PointsToWithInvalidate.h"
#include "analysis/PointsTo/Pointer.h"
#include "analysis/Offset.h"

namespace llvm {
    class Module;
    class Function;
}

namespace dg {
namespace llvmdg {

using analysis::LLVMPointerAnalysisOptions;
using analysis::LLVMReachingDefinitionsAnalysisOptions;

struct LLVMDependenceGraphOptions {
    LLVMPointerAnalysisOptions PTAOptions{};
    LLVMReachingDefinitionsAnalysisOptions RDAOptions{};

    CD_ALG cdAlgorithm{CD_ALG::CLASSIC};

    bool verifyGraph{true};
    bool DUUndefinedArePure{false};
    const std::string entryFunction{"main"};
};

class LLVMDependenceGraphBuilder {
    llvm::Module *_M;
    const LLVMDependenceGraphOptions _options;
    std::unique_ptr<LLVMPointerAnalysis> _PTA{};
    std::unique_ptr<LLVMReachingDefinitions> _RD{};
    std::unique_ptr<LLVMDependenceGraph> _dg{};
    llvm::Function *_entryFunction{nullptr};

    void _runPointerAnalysis() {
        assert(_PTA && "BUG: No PTA");

        if (_options.PTAOptions.isFS())
            _PTA->run<analysis::pta::PointsToFlowSensitive>();
        else if (_options.PTAOptions.isFI())
            _PTA->run<analysis::pta::PointsToFlowInsensitive>();
        else if (_options.PTAOptions.isFSInv())
            _PTA->run<analysis::pta::PointsToWithInvalidate>();
        else {
            assert(0 && "Wrong pointer analysis");
            abort();
        }
    }

    void _runReachingDefinitionsAnalysis() {
        assert(_RD && "BUG: No RD");

        if (_options.RDAOptions.isDense()) {
            _RD->run<dg::analysis::rd::ReachingDefinitionsAnalysis>();
        } else if (_options.RDAOptions.isSparse()) {
            _RD->run<dg::analysis::rd::SemisparseRda>();
        } else {
            assert( false && "unknown RDA type" );
            abort();
        }
    }

    void _runDefUseAnalysis() {
        LLVMDefUseAnalysis DUA(_dg.get(),
                               _RD.get(),
                               _PTA.get(),
                               // FIXME: this should go to DU Options
                               _options.DUUndefinedArePure);
        DUA.run(); // add def-use edges according that
    }

    void _runControlDependenceAnalysis() {
        _dg->computeControlDependencies(_options.cdAlgorithm);
    }

    bool verify() const {
        return _dg->verify();
    }

public:
    LLVMDependenceGraphBuilder(llvm::Module *M)
    : LLVMDependenceGraphBuilder(M, {}) {}

    LLVMDependenceGraphBuilder(llvm::Module *M,
                               const LLVMDependenceGraphOptions& opts)
    : _M(M), _options(opts),
      _PTA(new LLVMPointerAnalysis(M, _options.PTAOptions)),
      _RD(new LLVMReachingDefinitions(M, _PTA.get(),
                                      _options.RDAOptions)),
      _dg(new LLVMDependenceGraph()),
      _entryFunction(M->getFunction(_options.entryFunction)) {
        assert(_entryFunction && "The entry function not found");
    }

    LLVMPointerAnalysis *getPTA() { return _PTA.get(); }
    LLVMReachingDefinitions *getRDA() { return _RD.get(); }

    // construct the whole graph with all edges
    std::unique_ptr<LLVMDependenceGraph>&& build() {
        // data dependencies
        _runPointerAnalysis();
        _runReachingDefinitionsAnalysis();
        _runDefUseAnalysis();

        // build the graph itself
        _dg->build(_M, _PTA.get(), _RD.get(), _entryFunction);

        // fill-in control dependencies
        _runControlDependenceAnalysis();

        // verify if the graph is built correctly
        if (_options.verifyGraph && !_dg->verify()) {
            _dg.reset();
            return std::move(_dg);
        }

        return std::move(_dg);
    }

    // Build only the graph with CFG edges.
    // No dependencies between instructions are added.
    // The dependencies must be filled in by calling computeDependencies()
    // later.
    // NOTE: this function still runs pointer analysis as it is needed
    // for sound construction of CFG in the presence of function pointer calls.
    std::unique_ptr<LLVMDependenceGraph>&& constructCFGOnly() {
        // data dependencies
        _runPointerAnalysis();

        // build the graph itself
        _dg->build(_M, _PTA.get(), _RD.get(), _entryFunction);

        // verify if the graph is built correctly
        if (_options.verifyGraph && !_dg->verify()) {
            _dg.reset();
            return std::move(_dg);
        }

        return std::move(_dg);
    }

    // This method serves to finish the graph construction
    // after constructCFGOnly was used to build the graph.
    // This function takes the dg (returned from the constructCFGOnly)
    // and retains its ownership until it computes the edges.
    // Then it returns the ownership back to the caller.
    std::unique_ptr<LLVMDependenceGraph>&&
    computeDependencies(std::unique_ptr<LLVMDependenceGraph>&& dg) {
        // get the ownership
        _dg = std::move(dg);

        // data-dependence edges
        _runReachingDefinitionsAnalysis();
        _runDefUseAnalysis();

        // fill-in control dependencies
        _runControlDependenceAnalysis();

        return std::move(_dg);
    }

};

} // namespace llvmdg
} // namespace dg

#endif // _DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_
