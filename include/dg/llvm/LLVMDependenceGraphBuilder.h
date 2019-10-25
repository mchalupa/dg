#ifndef _DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_
#define _DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_

#include <string>
#include <ctime> // std::clock

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
#include "dg/llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h"
#include "dg/llvm/analysis/DataDependence/DataDependence.h"
#include "dg/llvm/analysis/DataDependence/LLVMDataDependenceAnalysisOptions.h"

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"
#ifdef HAVE_SVF
#include "dg/llvm/analysis/PointsTo/SVFPointerAnalysis.h"
#endif
#include "dg/analysis/PointsTo/PointerAnalysisFI.h"
#include "dg/analysis/PointsTo/PointerAnalysisFS.h"
#include "dg/analysis/PointsTo/PointerAnalysisFSInv.h"
#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/analysis/Offset.h"

#include "dg/llvm/analysis/ThreadRegions/ControlFlowGraph.h"

namespace llvm {
    class Module;
    class Function;
}

namespace dg {
namespace llvmdg {

struct LLVMDependenceGraphOptions {
    LLVMPointerAnalysisOptions PTAOptions{};
    LLVMDataDependenceAnalysisOptions DDAOptions{};

    bool terminationSensitive{true};
    CD_ALG cdAlgorithm{CD_ALG::CLASSIC};

    bool verifyGraph{true};

    bool threads{false};

    std::string entryFunction{"main"};

    void addAllocationFunction(const std::string& name,
                               AllocationFunction F) {
        PTAOptions.addAllocationFunction(name, F);
        DDAOptions.addAllocationFunction(name, F);
    }
};

class LLVMDependenceGraphBuilder {
    llvm::Module *_M;
    const LLVMDependenceGraphOptions _options;
    std::unique_ptr<LLVMPointerAnalysis> _PTA{};
    std::unique_ptr<LLVMDataDependenceAnalysis> _DDA{nullptr};
    std::unique_ptr<LLVMDependenceGraph> _dg{};
    std::unique_ptr<ControlFlowGraph> _controlFlowGraph{};
    llvm::Function *_entryFunction{nullptr};

    struct Statistics {
        uint64_t cdTime{0};
        uint64_t ptaTime{0};
        uint64_t rdaTime{0};
        uint64_t inferaTime{0};
        uint64_t joinsTime{0};
        uint64_t critsecTime{0};
    } _statistics;

    std::clock_t _time_start;
    void _timerStart() { _time_start = std::clock(); }
    uint64_t _timerEnd() { return (std::clock() - _time_start); }

    void _runPointerAnalysis() {
        assert(_PTA && "BUG: No PTA");

        _timerStart();
        _PTA->run();
        _statistics.ptaTime = _timerEnd();
    }

    void _runDataDependenceAnalysis() {
        assert(_DDA && "BUG: No RD");

        _timerStart();
        _DDA->run();
        _statistics.rdaTime = _timerEnd();
    }

    void _runControlDependenceAnalysis() {
        _timerStart();
        _dg->computeControlDependencies(_options.cdAlgorithm,
                                        _options.terminationSensitive);
        _statistics.cdTime = _timerEnd();
    }

    void _runInterferenceDependenceAnalysis() {
        _timerStart();
        _dg->computeInterferenceDependentEdges(_controlFlowGraph.get());
        _statistics.inferaTime = _timerEnd();
    }

    void _runForkJoinAnalysis() {
        _timerStart();
        _dg->computeForkJoinDependencies(_controlFlowGraph.get());
        _statistics.joinsTime = _timerEnd();
    }

    void _runCriticalSectionAnalysis() {
        _timerStart();
        _dg->computeCriticalSections(_controlFlowGraph.get());
        _statistics.critsecTime = _timerEnd();
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
      _PTA(createPTA()),
      _DDA(new LLVMDataDependenceAnalysis(M, _PTA.get(),
                                          _options.DDAOptions)),
      _dg(new LLVMDependenceGraph(opts.threads)),
      _controlFlowGraph(_options.threads && !_options.PTAOptions.isSVF() ? // check SVF due to the static cast...
            new ControlFlowGraph(static_cast<DGLLVMPointerAnalysis*>(_PTA.get())) : nullptr),
      _entryFunction(M->getFunction(_options.entryFunction)) {
        assert(_entryFunction && "The entry function not found");
    }

    LLVMPointerAnalysis *createPTA() {
        if (_options.PTAOptions.isSVF())
            return new SVFPointerAnalysis(_M, _options.PTAOptions);

        return new DGLLVMPointerAnalysis(_M, _options.PTAOptions);
    }

    LLVMPointerAnalysis *getPTA() { return _PTA.get(); }
    LLVMDataDependenceAnalysis *getRDA() { return _DDA.get(); }

    const Statistics& getStatistics() const { return _statistics; }

    // construct the whole graph with all edges
    std::unique_ptr<LLVMDependenceGraph>&& build() {
        // compute data dependencies
        _runPointerAnalysis();
        _runDataDependenceAnalysis();

        // build the graph itself (the nodes, but without edges)
        _dg->build(_M, _PTA.get(), _DDA.get(), _entryFunction);

        // insert the data dependencies edges
        _dg->addDefUseEdges();

        // compute and fill-in control dependencies
        _runControlDependenceAnalysis();

        if (_options.threads) {
            if (_options.PTAOptions.isSVF()) {
                assert(0 && "Threading needs the DG pointer analysis, SVF is not supported yet");
                abort();
            }
            _controlFlowGraph->buildFunction(_entryFunction);
            _runInterferenceDependenceAnalysis();
            _runForkJoinAnalysis();
            _runCriticalSectionAnalysis();
        }

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
        _dg->build(_M, _PTA.get(), _DDA.get(), _entryFunction);

        if (_options.threads) {
            _controlFlowGraph->buildFunction(_entryFunction);
        }

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
        _runDataDependenceAnalysis();
        _dg->addDefUseEdges();

        // fill-in control dependencies
        _runControlDependenceAnalysis();

        if (_options.threads) {
            _runInterferenceDependenceAnalysis();
            _runForkJoinAnalysis();
            _runCriticalSectionAnalysis();
        }

        return std::move(_dg);
    }

};

} // namespace llvmdg
} // namespace dg

#endif // _DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_
