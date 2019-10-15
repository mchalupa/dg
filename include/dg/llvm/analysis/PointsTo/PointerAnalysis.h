#ifndef _LLVM_DG_POINTS_TO_ANALYSIS_H_
#define _LLVM_DG_POINTS_TO_ANALYSIS_H_

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/raw_ostream.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/analysis/PointsTo/PointerGraph.h"
#include "dg/analysis/PointsTo/PointerAnalysis.h"
#include "dg/analysis/PointsTo/PointerGraphOptimizations.h"
#include "dg/analysis/PointsTo/PointerAnalysisFI.h"
#include "dg/analysis/PointsTo/PointerAnalysisFS.h"
#include "dg/analysis/PointsTo/PointerAnalysisFSInv.h"

#include "dg/llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h"
#include "dg/llvm/analysis/PointsTo/PointerGraph.h"
#include "dg/llvm/analysis/PointsTo/LLVMPointsToSet.h"


namespace dg {

using analysis::LLVMPointerAnalysisOptions;
using analysis::pta::PointerGraph;
using analysis::pta::PSNode;
using analysis::pta::LLVMPointerGraphBuilder;
using analysis::pta::Pointer;
using analysis::Offset;

///
// Interface for LLVM pointer analysis
class LLVMPointerAnalysis {
protected:
    const LLVMPointerAnalysisOptions options{};

    LLVMPointerAnalysis(const LLVMPointerAnalysisOptions& opts)
        : options(opts) {};
public:
    const LLVMPointerAnalysisOptions& getOptions() const { return options; }

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
    virtual std::pair<bool, LLVMPointsToSet> getLLVMPointsToChecked(const llvm::Value *val) = 0;

    virtual void run() = 0;

    virtual ~LLVMPointerAnalysis() = default;
};

template <typename PTType>
class DGLLVMPointerAnalysisImpl : public PTType
{
    LLVMPointerGraphBuilder *builder;

public:
    DGLLVMPointerAnalysisImpl(PointerGraph *PS, LLVMPointerGraphBuilder *b)
    : PTType(PS), builder(b) {}

    // build new subgraphs on calls via pointer
    bool functionPointerCall(PSNode *callsite, PSNode *called) override {
        using namespace analysis::pta;
        const llvm::Function *F
            = llvm::dyn_cast<llvm::Function>(called->getUserData<llvm::Value>());
        // with vararg it may happen that we get pointer that
        // is not to function, so just bail out here in that case
        if (!F)
            return false;

        if (F->isDeclaration()) {
            if (builder->threads()) {
                if (F->getName() == "pthread_create") {
                    builder->insertPthreadCreateByPtrCall(callsite);
                    return true;
                } else if (F->getName() == "pthread_join") {
                    builder->insertPthreadJoinByPtrCall(callsite);
                    return true;
                }
            }
            return callsite->getPairedNode()->addPointsTo(analysis::pta::UnknownPointer);
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
            llvm::errs() << "This happend after building this function called via pointer: "
                         <<  F->getName() << "\n";
            abort();
        }
#endif // NDEBUG

        return true; // we changed the graph
    }

    bool handleFork(PSNode *forkNode, PSNode *called) override {
        using namespace llvm;
        using namespace dg::analysis::pta;

        assert(called->getType() == PSNodeType::FUNCTION
                && "The called value is not a function");

        PSNodeFork *fork = PSNodeFork::get(forkNode);
        builder->addFunctionToFork(called, fork);

#ifndef NDEBUG
        // check the graph after rebuilding, but do not check for connectivity,
        // because we can call a function that will disconnect the graph
        if (!builder->validateSubgraph(true)) {
            const llvm::Function *F
                = llvm::cast<llvm::Function>(called->getUserData<llvm::Value>());
            llvm::errs() << "Pointer Subgraph is broken!\n";
            llvm::errs() << "This happend after building this function spawned in a thread: "
                         <<  F->getName() << "\n";
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
    std::unique_ptr<LLVMPointerGraphBuilder> _builder;

    LLVMPointerAnalysisOptions createOptions(const char *entry_func,
                                             uint64_t field_sensitivity,
                                             bool threads = false)
    {
        LLVMPointerAnalysisOptions opts;
        opts.threads = threads;
        opts.setFieldSensitivity(field_sensitivity);
        opts.setEntryFunction(entry_func);
        return opts;
    }

    const PointsToSetT& getUnknownPTSet() const {
        static const PointsToSetT _unknownPTSet
            = PointsToSetT({Pointer{analysis::pta::UNKNOWN_MEMORY, 0}});
        return _unknownPTSet;
    }

public:

    DGLLVMPointerAnalysis(const llvm::Module *m,
                          const char *entry_func = "main",
                          uint64_t field_sensitivity = Offset::UNKNOWN,
                          bool threads = false)
        : DGLLVMPointerAnalysis(m, createOptions(entry_func, field_sensitivity, threads)) {}

    DGLLVMPointerAnalysis(const llvm::Module *m, const LLVMPointerAnalysisOptions opts)
        : LLVMPointerAnalysis(opts), _builder(new LLVMPointerGraphBuilder(m, opts)) {}

    ///
    // Get the node from pointer analysis that holds the points-to set.
    // See: getLLVMPointsTo()
    PSNode *getPointsToNode(const llvm::Value *val) const {
        return _builder->getPointsTo(val);
    }

    inline bool threads() const { return _builder->threads(); }

    bool hasPointsTo(const llvm::Value *val) override {
        if (auto node = getPointsToNode(val)) {
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
        if (auto node = getPointsToNode(val)) {
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
        if (auto node = getPointsToNode(val)) {
            if (node->pointsTo.empty()) {
                pts = new DGLLVMPointsToSet(getUnknownPTSet());
                return {false, pts->toLLVMPointsToSet()};
            } else {
                pts = new DGLLVMPointsToSet(node->pointsTo);
                return {true, pts->toLLVMPointsToSet()};
            }
        } else {
            pts = new DGLLVMPointsToSet(getUnknownPTSet());
            return {false, pts->toLLVMPointsToSet()};
        }
    }

    const std::vector<std::unique_ptr<PSNode>>& getNodes()
    {
        return PS->getNodes();
    }

    std::vector<PSNode *> getFunctionNodes(const llvm::Function *F) const {
        return _builder->getFunctionNodes(F);
    }

    PointerGraph *getPS() { return PS; }
    const PointerGraph *getPS() const { return PS; }

    LLVMPointerGraphBuilder *getBuilder() { return _builder.get(); }
    const LLVMPointerGraphBuilder *getBuilder() const { return _builder.get(); }

    void buildSubgraph()
    {
        // run the analysis itself
        assert(_builder && "Incorrectly constructed PTA, missing builder");

        PS = _builder->buildLLVMPointerGraph();
        if (!PS) {
            llvm::errs() << "Pointer Subgraph was not built, aborting\n";
            abort();
        }

/*
        analysis::pta::PointerGraphOptimizer optimizer(PS);
        optimizer.run();

        if (optimizer.getNumOfRemovedNodes() > 0)
            _builder->composeMapping(std::move(optimizer.getMapping()));

        llvm::errs() << "PS optimization removed " << optimizer.getNumOfRemovedNodes() << " nodes\n";

#ifndef NDEBUG
        analysis::pta::debug::LLVMPointerGraphValidator validator(_builder->getPS());
        if (validator.validate()) {
            llvm::errs() << "Pointer Subgraph is broken!\n";
            llvm::errs() << "This happend after optimizing the graph.";
            assert(!validator.getErrors().empty());
            llvm::errs() << validator.getErrors();
            abort();
        }
#endif // NDEBUG
*/

    }

    void run() override {
        if (options.isFSInv())
            _builder->setInvalidateNodesFlag(true);

        buildSubgraph();

        if (options.isFS()) {
            // FIXME: make a interface with run() method
            DGLLVMPointerAnalysisImpl<analysis::pta::PointerAnalysisFS> PTA(PS, _builder.get());
            PTA.run();
        } else if (options.isFI()) {
            DGLLVMPointerAnalysisImpl<analysis::pta::PointerAnalysisFI> PTA(PS, _builder.get());
            PTA.run();
        } else if (options.isFSInv()) {
            DGLLVMPointerAnalysisImpl<analysis::pta::PointerAnalysisFSInv> PTA(PS, _builder.get());
            PTA.run();
        } else {
            assert(0 && "Wrong pointer analysis");
            abort();
        }
    }

    // this method creates PointerAnalysis object and returns it.
    // It is alternative to run() method, but it does not delete all
    // the analysis data as the run() (like memory objects and so on).
    // run() preserves only PointerGraph and the builder
    template <typename PTType>
    analysis::pta::PointerAnalysis *createPTA()
    {
        buildSubgraph();
        return new DGLLVMPointerAnalysisImpl<PTType>(PS, _builder.get());
    }
};

// an auxiliary function
inline std::vector<const llvm::Function *>
getCalledFunctions(const llvm::Value *calledValue, LLVMPointerAnalysis *PTA) {
    std::vector<const llvm::Function *> functions;
    for (const auto& llvmptr : PTA->getLLVMPointsTo(calledValue)) {
        if (const auto F = llvm::dyn_cast<llvm::Function>(llvmptr.value)) {
            functions.push_back(F);
        }
    }
    return functions;
}

} // namespace dg

#endif // _LLVM_DG_POINTS_TO_ANALYSIS_H_
