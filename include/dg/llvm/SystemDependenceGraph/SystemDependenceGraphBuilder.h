#ifndef DG_LLVM_SYSTEM_DEPENDENCE_GRAPH_BUILDER_H_
#define DG_LLVM_SYSTEM_DEPENDENCE_GRAPH_BUILDER_H_

#include <ctime> // std::clock
#include <string>

#include <llvm/IR/Module.h>

#include "dg/llvm/DataDependence/DataDependence.h"
#include "dg/llvm/DataDependence/LLVMDataDependenceAnalysisOptions.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/PointerAnalysis/LLVMPointerAnalysisOptions.h"

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#ifdef HAVE_SVF
#include "dg/llvm/PointerAnalysis/SVFPointerAnalysis.h"
#endif
#include "dg/Offset.h"
#include "dg/PointerAnalysis/Pointer.h"
#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"
#include "dg/PointerAnalysis/PointerAnalysisFSInv.h"

namespace llvm {
class Module;
class Function;
} // namespace llvm

namespace dg {
namespace llvmdg {

class LLVMPointerAnalysisOptions;
class LLVMDataDependenceAnalysisOptions;

class SystemDependenceGraphBuilder {
    llvm::Module *_M;
    const SystemDependenceGraphOptions _options;
    std::unique_ptr<LLVMPointerAnalysis> _PTA{};
    std::unique_ptr<LLVMDataDependenceAnalysis> _DDA{nullptr};
    std::unique_ptr<SystemDependenceGraph> _sdg{};
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
        _sdg->computeControlDependencies(_options.cdAlgorithm,
                                         _options.interprocCd);
        _statistics.cdTime = _timerEnd();
    }

    void _runInterferenceDependenceAnalysis() {
        _timerStart();
        _sdg->computeInterferenceDependentEdges(_controlFlowGraph.get());
        _statistics.inferaTime = _timerEnd();
    }

    void _runForkJoinAnalysis() {
        _timerStart();
        _sdg->computeForkJoinDependencies(_controlFlowGraph.get());
        _statistics.joinsTime = _timerEnd();
    }

    void _runCriticalSectionAnalysis() {
        _timerStart();
        _sdg->computeCriticalSections(_controlFlowGraph.get());
        _statistics.critsecTime = _timerEnd();
    }

    bool verify() const { return _sdg->verify(); }

  public:
    SystemDependenceGraphBuilder(llvm::Module *M)
            : SystemDependenceGraphBuilder(M, {}) {}

    SystemDependenceGraphBuilder(llvm::Module *M,
                                 const SystemDependenceGraphOptions &opts)
            : _M(M), _options(opts), _PTA(createPTA()),
              _DDA(new LLVMDataDependenceAnalysis(M, _PTA.get(),
                                                  _options.DDAOptions)),
              _sdg(new LLVMDependenceGraph(opts.threads)),
              _controlFlowGraph(
                      _options.threads && !_options.PTAOptions.isSVF()
                              ? // check SVF due to the static cast...
                              new ControlFlowGraph(
                                      static_cast<DGLLVMPointerAnalysis *>(
                                              _PTA.get()))
                              : nullptr),
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

    const Statistics &getStatistics() const { return _statistics; }

    // construct the whole graph with all edges
    std::unique_ptr<SystemDependenceGraph> &&build() {
        // compute data dependencies
        _runPointerAnalysis();
        _runDataDependenceAnalysis();

        // build the graph itself (the nodes, but without edges)
        _sdg->build(_M, _PTA.get(), _DDA.get(), _entryFunction);

        // insert the data dependencies edges
        _sdg->addDefUseEdges();

        // compute and fill-in control dependencies
        _runControlDependenceAnalysis();

        if (_options.threads) {
            if (_options.PTAOptions.isSVF()) {
                assert(0 && "Threading needs the DG pointer analysis, SVF is "
                            "not supported yet");
                abort();
            }
            _controlFlowGraph->buildFunction(_entryFunction);
            _runInterferenceDependenceAnalysis();
            _runForkJoinAnalysis();
            _runCriticalSectionAnalysis();
        }

        // verify if the graph is built correctly
        if (_options.verifyGraph && !_sdg->verify()) {
            _sdg.reset();
            return std::move(_sdg);
        }

        return std::move(_sdg);
    }

    // Build only the graph with CFG edges.
    // No dependencies between instructions are added.
    // The dependencies must be filled in by calling computeDependencies()
    // later.
    // NOTE: this function still runs pointer analysis as it is needed
    // for sound construction of CFG in the presence of function pointer calls.
    std::unique_ptr<LLVMDependenceGraph> &&constructCFGOnly() {
        // data dependencies
        _runPointerAnalysis();

        // build the graph itself
        _sdg->build(_M, _PTA.get(), _DDA.get(), _entryFunction);

        if (_options.threads) {
            _controlFlowGraph->buildFunction(_entryFunction);
        }

        // verify if the graph is built correctly
        if (_options.verifyGraph && !_sdg->verify()) {
            _sdg.reset();
            return std::move(_sdg);
        }

        return std::move(_sdg);
    }

    // This method serves to finish the graph construction
    // after constructCFGOnly was used to build the graph.
    // This function takes the dg (returned from the constructCFGOnly)
    // and retains its ownership until it computes the edges.
    // Then it returns the ownership back to the caller.
    std::unique_ptr<LLVMDependenceGraph> &&
    computeDependencies(std::unique_ptr<LLVMDependenceGraph> &&dg) {
        // get the ownership
        _sdg = std::move(dg);

        // data-dependence edges
        _runDataDependenceAnalysis();
        _sdg->addDefUseEdges();

        // fill-in control dependencies
        _runControlDependenceAnalysis();

        if (_options.threads) {
            _runInterferenceDependenceAnalysis();
            _runForkJoinAnalysis();
            _runCriticalSectionAnalysis();
        }

        return std::move(_sdg);
    }
};

} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_SYSTEM_DEPENDENCE_GRAPH_BUILDER_H_
