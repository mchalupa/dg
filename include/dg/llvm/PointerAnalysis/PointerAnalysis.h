#ifndef LLVM_DG_POINTS_TO_ANALYSIS_H_
#define LLVM_DG_POINTS_TO_ANALYSIS_H_

#include <memory>
#include <utility>

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>

#include "dg/PointerAnalysis/Pointer.h"
#include "dg/PointerAnalysis/PointerAnalysis.h"
#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"
#include "dg/PointerAnalysis/PointerAnalysisFSInv.h"
#include "dg/PointerAnalysis/PointerGraph.h"
#include "dg/PointerAnalysis/PointerGraphOptimizations.h"

#include "dg/llvm/PointerAnalysis/LLVMPointerAnalysisOptions.h"
#include "dg/llvm/PointerAnalysis/LLVMPointsToSet.h"
#include "dg/llvm/PointerAnalysis/PointerGraph.h"

namespace dg {

using pta::LLVMPointerGraphBuilder;
using pta::Pointer;
using pta::PointerGraph;
using pta::PSNode;

///
// Interface for LLVM pointer analysis
class LLVMPointerAnalysis {
  protected:
    const LLVMPointerAnalysisOptions options{};

    LLVMPointerAnalysis(LLVMPointerAnalysisOptions opts)
            : options(std::move(opts)){};

  public:
    const LLVMPointerAnalysisOptions &getOptions() const { return options; }

    ///
    // This method returns true if the pointer analysis
    //  1) has any points-to set associated to the value 'val'
    //  2) the points-to set is non-empty
    virtual bool hasPointsTo(const llvm::Value *val) = 0;

    ///
    // Get the points-to information for the given LLVM value.
    // The return object has methods begin(), end() that can be used
    // for iteration over (llvm::Value *, Offset) pairs of the
    // points-to set. Moreover, the object has methods hasUnknown(),
    // hasNull(), and hasInvalidated() that reflect whether the points-to set of
    // the LLVM value contains unknown element, null, or invalidated.
    // If the pointer analysis has no or empty points-to set for 'val'
    // (i.e. hasPointsTo() returns false), a points-to set containing
    // the only element unknown is returned.
    virtual LLVMPointsToSet getLLVMPointsTo(const llvm::Value *val) = 0;

    ///
    // This method is the same as getLLVMPointsTo, but it returns
    // also the result of hasPointsTo(), so one can check whether the unknown
    // pointer in the set is present because hasPointsTo() was false,
    // or wether it has been propagated there during the analysis.
    virtual std::pair<bool, LLVMPointsToSet>
    getLLVMPointsToChecked(const llvm::Value *val) = 0;

    // A convenient wrapper around getLLVMPointsTo
    // that takes an instruction and returns a set of
    // memory regions that are accessed (read/written)
    // by this instruction. It also returns a boolean
    // set to true if this information is not known (i.e.,
    // the points-to set did contain 'unknown' element
    // or was empty). For CallInst, it returns memory
    // regions that may be accessed via the passed arguments.
    std::pair<bool, LLVMMemoryRegionSet>
    getAccessedMemory(const llvm::Instruction *I);

    virtual bool run() = 0;

    virtual ~LLVMPointerAnalysis() = default;
};

template <typename PTType>
class DGLLVMPointerAnalysisImpl : public PTType {
    LLVMPointerGraphBuilder *builder;

  public:
    DGLLVMPointerAnalysisImpl(PointerGraph *PS, LLVMPointerGraphBuilder *b)
            : PTType(PS), builder(b) {}

    DGLLVMPointerAnalysisImpl(PointerGraph *PS, LLVMPointerGraphBuilder *b,
                              const LLVMPointerAnalysisOptions &opts)
            : PTType(PS, opts), builder(b) {}

    // build new subgraphs on calls via pointer
    bool functionPointerCall(PSNode *callsite, PSNode *called) override {
        using namespace pta;
        const llvm::Function *F = llvm::dyn_cast<llvm::Function>(
                called->getUserData<llvm::Value>());
        // with vararg it may happen that we get pointer that
        // is not to function, so just bail out here in that case
        if (!F)
            return false;

        if (F->isDeclaration()) {
            if (builder->threads()) {
                if (F->getName() == "pthread_create") {
                    builder->insertPthreadCreateByPtrCall(callsite);
                    return true;
                }
                if (F->getName() == "pthread_join") {
                    builder->insertPthreadJoinByPtrCall(callsite);
                    return true;
                }
            }
        }

        if (!LLVMPointerGraphBuilder::callIsCompatible(callsite, called)) {
            return false;
        }

        builder->insertFunctionCall(callsite, called);

        // call the original handler that works on generic graphs
        PTType::functionPointerCall(callsite, called);

#ifndef NDEBUG
        // check the graph after rebuilding, but do not check for connectivity,
        // because we can call a function that will disconnect the graph
        if (!builder->validateSubgraph(true)) {
            llvm::errs() << "Pointer Subgraph is broken!\n";
            llvm::errs() << "This happend after building this function called "
                            "via pointer: "
                         << F->getName() << "\n";
            abort();
        }
#endif // NDEBUG

        return true; // we changed the graph
    }

    bool handleFork(PSNode *forkNode, PSNode *called) override {
        using namespace llvm;
        using namespace dg::pta;

        assert(called->getType() == PSNodeType::FUNCTION &&
               "The called value is not a function");

        PSNodeFork *fork = PSNodeFork::get(forkNode);
        builder->addFunctionToFork(called, fork);

#ifndef NDEBUG
        // check the graph after rebuilding, but do not check for connectivity,
        // because we can call a function that will disconnect the graph
        if (!builder->validateSubgraph(true)) {
            const llvm::Function *F = llvm::cast<llvm::Function>(
                    called->getUserData<llvm::Value>());
            llvm::errs() << "Pointer Subgraph is broken!\n";
            llvm::errs() << "This happend after building this function spawned "
                            "in a thread: "
                         << F->getName() << "\n";
            abort();
        }
#endif // NDEBUG

        return true;
    }

    bool handleJoin(PSNode *joinNode) override {
        return builder->matchJoinToRightCreate(joinNode);
    }
};

class DGLLVMPointerAnalysis : public LLVMPointerAnalysis {
    PointerGraph *PS = nullptr;
    std::unique_ptr<pta::PointerAnalysis> PTA{}; // dg pointer analysis object
    std::unique_ptr<LLVMPointerGraphBuilder> _builder;

    static LLVMPointerAnalysisOptions createOptions(const char *entry_func,
                                                    uint64_t field_sensitivity,
                                                    bool threads = false) {
        LLVMPointerAnalysisOptions opts;
        opts.threads = threads;
        opts.setFieldSensitivity(field_sensitivity);
        opts.setEntryFunction(entry_func);
        return opts;
    }

    static const PointsToSetT &getUnknownPTSet() {
        static const PointsToSetT _unknownPTSet =
                PointsToSetT({Pointer{pta::UNKNOWN_MEMORY, 0}});
        return _unknownPTSet;
    }

  public:
    DGLLVMPointerAnalysis(const llvm::Module *m,
                          const char *entry_func = "main",
                          uint64_t field_sensitivity = Offset::UNKNOWN,
                          bool threads = false)
            : DGLLVMPointerAnalysis(
                      m,
                      createOptions(entry_func, field_sensitivity, threads)) {}

    DGLLVMPointerAnalysis(const llvm::Module *m,
                          const LLVMPointerAnalysisOptions opts)
            : LLVMPointerAnalysis(opts),
              _builder(new LLVMPointerGraphBuilder(m, opts)) {}

    ///
    // Get the node from pointer analysis that holds the points-to set.
    // See: getLLVMPointsTo()
    PSNode *getPointsToNode(const llvm::Value *val) const {
        return _builder->getPointsToNode(val);
    }

    pta::PointerAnalysis *getPTA() { return PTA.get(); }
    const pta::PointerAnalysis *getPTA() const { return PTA.get(); }

    bool threads() const { return _builder->threads(); }

    bool hasPointsTo(const llvm::Value *val) override {
        if (auto *node = getPointsToNode(val)) {
            return !node->pointsTo.empty();
        }
        return false;
    }

    ///
    // Get the points-to information for the given LLVM value.
    // The return object has methods begin(), end() that can be used
    // for iteration over (llvm::Value *, Offset) pairs of the
    // points-to set. Moreover, the object has methods hasUnknown()
    // and hasNull() that reflect whether the points-to set of the
    // LLVM value contains unknown element of null.
    LLVMPointsToSet getLLVMPointsTo(const llvm::Value *val) override {
        DGLLVMPointsToSet *pts;
        if (auto *node = getPointsToNode(val)) {
            if (node->pointsTo.empty()) {
                pts = new DGLLVMPointsToSet(getUnknownPTSet());
            } else {
                pts = new DGLLVMPointsToSet(node->pointsTo);
            }
        } else {
            pts = new DGLLVMPointsToSet(getUnknownPTSet());
        }
        return pts->toLLVMPointsToSet();
    }

    ///
    // This method is the same as getLLVMPointsTo, but it returns
    // also the information whether the node of pointer analysis exists
    // (opposed to the getLLVMPointsTo, which returns a set with
    // unknown element when the node does not exists)
    std::pair<bool, LLVMPointsToSet>
    getLLVMPointsToChecked(const llvm::Value *val) override {
        DGLLVMPointsToSet *pts;
        if (auto *node = getPointsToNode(val)) {
            if (node->pointsTo.empty()) {
                pts = new DGLLVMPointsToSet(getUnknownPTSet());
                return {false, pts->toLLVMPointsToSet()};
            }
            pts = new DGLLVMPointsToSet(node->pointsTo);
            return {true, pts->toLLVMPointsToSet()};
        }
        pts = new DGLLVMPointsToSet(getUnknownPTSet());
        return {false, pts->toLLVMPointsToSet()};
    }

    const std::vector<std::unique_ptr<PSNode>> &getNodes() {
        return PS->getNodes();
    }

    std::vector<PSNode *> getFunctionNodes(const llvm::Function *F) const {
        return _builder->getFunctionNodes(F);
    }

    PointerGraph *getPS() { return PS; }
    const PointerGraph *getPS() const { return PS; }

    LLVMPointerGraphBuilder *getBuilder() { return _builder.get(); }
    const LLVMPointerGraphBuilder *getBuilder() const { return _builder.get(); }

    void buildSubgraph() {
        // run the analysis itself
        assert(_builder && "Incorrectly constructed PTA, missing builder");

        PS = _builder->buildLLVMPointerGraph();
        if (!PS) {
            llvm::errs() << "Pointer Subgraph was not built, aborting\n";
            abort();
        }

        /*
        pta::PointerGraphOptimizer optimizer(PS);
        optimizer.run();

        if (optimizer.getNumOfRemovedNodes() > 0)
            _builder->composeMapping(std::move(optimizer.getMapping()));

        llvm::errs() << "PS optimization removed " <<
        optimizer.getNumOfRemovedNodes() << " nodes\n";
        */
    }

    void initialize() {
        if (options.isFSInv())
            _builder->setInvalidateNodesFlag(true);

        buildSubgraph();

        if (options.isFS()) {
            // FIXME: make a interface with run() method
            PTA.reset(new DGLLVMPointerAnalysisImpl<pta::PointerAnalysisFS>(
                    PS, _builder.get(), options));
        } else if (options.isFI()) {
            PTA.reset(new DGLLVMPointerAnalysisImpl<pta::PointerAnalysisFI>(
                    PS, _builder.get(), options));
        } else if (options.isFSInv()) {
            PTA.reset(new DGLLVMPointerAnalysisImpl<pta::PointerAnalysisFSInv>(
                    PS, _builder.get(), options));
        } else {
            assert(0 && "Wrong pointer analysis");
            abort();
        }
    }

    bool run() override {
        if (!PTA) {
            initialize();
        }
        return PTA->run();
    }
};

// an auxiliary function
inline std::vector<const llvm::Function *>
getCalledFunctions(const llvm::Value *calledValue, LLVMPointerAnalysis *PTA) {
    std::vector<const llvm::Function *> functions;
    for (const auto &llvmptr : PTA->getLLVMPointsTo(calledValue)) {
        if (auto *const F = llvm::dyn_cast<llvm::Function>(llvmptr.value)) {
            functions.push_back(F);
        }
    }
    return functions;
}

} // namespace dg

#endif // LLVM_DG_POINTS_TO_ANALYSIS_H_
