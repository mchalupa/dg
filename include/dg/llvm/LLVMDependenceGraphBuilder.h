#ifndef _DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_
#define _DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_

#include <string>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Module.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/analysis/DefUse/LLVMDefUseAnalysisOptions.h"
#include "dg/llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h"
#include "dg/llvm/analysis/ReachingDefinitions/LLVMReachingDefinitionsAnalysisOptions.h"

#include "dg/llvm/analysis/DefUse/DefUse.h"
#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"
#include "dg/llvm/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/analysis/ReachingDefinitions/SemisparseRda.h"

#include "dg/analysis/PointsTo/PointerAnalysisFI.h"
#include "dg/analysis/PointsTo/PointerAnalysisFS.h"
#include "dg/analysis/PointsTo/PointerAnalysisFSInv.h"
#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/analysis/Offset.h"

namespace llvm {
    class Module;
    class Function;
}

namespace dg {
namespace llvmdg {

using analysis::LLVMDefUseAnalysisOptions;
using analysis::LLVMPointerAnalysisOptions;
using analysis::LLVMReachingDefinitionsAnalysisOptions;

struct LLVMDependenceGraphOptions {
    LLVMPointerAnalysisOptions PTAOptions{};
    LLVMReachingDefinitionsAnalysisOptions RDAOptions{};
    LLVMDefUseAnalysisOptions DUOptions{};

    bool terminationSensitive{true};
    CD_ALG cdAlgorithm{CD_ALG::CLASSIC};

    bool verifyGraph{true};
    std::string entryFunction{"main"};

    void addAllocationFunction(const std::string& name,
                               analysis::AllocationFunction F) {
        PTAOptions.addAllocationFunction(name, F);
        RDAOptions.addAllocationFunction(name, F);
        DUOptions.addAllocationFunction(name, F);
    }
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
            _PTA->run<analysis::pta::PointerAnalysisFS>();
        else if (_options.PTAOptions.isFI())
            _PTA->run<analysis::pta::PointerAnalysisFI>();
        else if (_options.PTAOptions.isFSInv())
            _PTA->run<analysis::pta::PointerAnalysisFSInv>();
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
                               _options.DUOptions);
        DUA.run(); // add def-use edges according that
    }

    void _runControlDependenceAnalysis() {
        _dg->computeControlDependencies(_options.cdAlgorithm,
                                        _options.terminationSensitive);
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
        // compute data dependencies
        _runPointerAnalysis();
        _runReachingDefinitionsAnalysis();

        // build the graph itself
        _dg->build(_M, _PTA.get(), _RD.get(), _entryFunction);

        // insert the data dependencies edges
        _runDefUseAnalysis();

        // compute and fill-in control dependencies
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
