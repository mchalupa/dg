#ifndef DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_
#define DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_

#include <ctime> // std::clock
#include <string>

#include <llvm/IR/Module.h>

#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisOptions.h"
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

#include "dg/llvm/ThreadRegions/ControlFlowGraph.h"

namespace llvm {
class Module;
class Function;
} // namespace llvm

namespace dg {
namespace llvmdg {

struct LLVMDependenceGraphOptions {
    LLVMPointerAnalysisOptions PTAOptions{};
    LLVMDataDependenceAnalysisOptions DDAOptions{};
    LLVMControlDependenceAnalysisOptions CDAOptions{};

    bool verifyGraph{true};
    bool threads{false};
    bool preserveDbg{true};

    std::string entryFunction{"main"};

    void addAllocationFunction(const std::string &name, AllocationFunction F) {
        PTAOptions.addAllocationFunction(name, F);
        DDAOptions.addAllocationFunction(name, F);
    }
};

class LLVMDependenceGraphBuilder {
    llvm::Module *_M;
    const LLVMDependenceGraphOptions _options;
    std::unique_ptr<LLVMPointerAnalysis> _PTA{};
    std::unique_ptr<LLVMDataDependenceAnalysis> _DDA{nullptr};
    std::unique_ptr<LLVMControlDependenceAnalysis> _CDA{nullptr};
    std::unique_ptr<LLVMDependenceGraph> _dg{};
    std::unique_ptr<ControlFlowGraph> _controlFlowGraph{};
    llvm::Function *_entryFunction{nullptr};

    struct Statistics {
        uint64_t cdaTime{0};
        uint64_t ptaTime{0};
        uint64_t rdaTime{0};
        uint64_t inferaTime{0};
        uint64_t joinsTime{0};
        uint64_t critsecTime{0};
    } _statistics;

    std::clock_t _time_start;
    void _timerStart() { _time_start = std::clock(); }
    uint64_t _timerEnd() const { return (std::clock() - _time_start); }

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
        //_CDA->run();
        // FIXME: until we get rid of the legacy code,
        // use the old way of inserting CD edges directly
        // into the dg
        _dg->computeControlDependencies(_options.CDAOptions);
        _statistics.cdaTime = _timerEnd();
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

    bool verify() const { return _dg->verify(); }

  public:
    LLVMDependenceGraphBuilder(llvm::Module *M)
            : LLVMDependenceGraphBuilder(M, {}) {}

    LLVMDependenceGraphBuilder(llvm::Module *M,
                               const LLVMDependenceGraphOptions &opts)
            : _M(M), _options(opts), _PTA(createPTA()),
              _DDA(new LLVMDataDependenceAnalysis(M, _PTA.get(),
                                                  _options.DDAOptions)),
              _CDA(new LLVMControlDependenceAnalysis(M, _options.CDAOptions)),
              _dg(new LLVMDependenceGraph(opts.threads)),
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
#ifdef HAVE_SVF
        if (_options.PTAOptions.isSVF())
            return new SVFPointerAnalysis(_M, _options.PTAOptions);
#endif

        return new DGLLVMPointerAnalysis(_M, _options.PTAOptions);
    }

    LLVMPointerAnalysis *getPTA() { return _PTA.get(); }
    LLVMDataDependenceAnalysis *getDDA() { return _DDA.get(); }

    const Statistics &getStatistics() const { return _statistics; }

    // construct the whole graph with all edges
    std::unique_ptr<LLVMDependenceGraph> &&build() {
        // compute data dependencies
        _runPointerAnalysis();
        _runDataDependenceAnalysis();

        // build the graph itself (the nodes, but without edges)
        _dg->build(_M, _PTA.get(), _DDA.get(), _entryFunction);

        // insert the data dependencies edges
        _dg->addDefUseEdges(_options.preserveDbg);

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
    std::unique_ptr<LLVMDependenceGraph> &&constructCFGOnly() {
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
    std::unique_ptr<LLVMDependenceGraph> &&
    computeDependencies(std::unique_ptr<LLVMDependenceGraph> &&dg) {
        // get the ownership
        _dg = std::move(dg);

        // data-dependence edges
        _runDataDependenceAnalysis();
        _dg->addDefUseEdges(_options.preserveDbg);

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

#endif // DG_LLVM_DEPENDENCE_GRAPH_BUILDER_H_
