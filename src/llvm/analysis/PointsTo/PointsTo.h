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

#include "analysis/PointsTo/PointerSubgraph.h"
#include "analysis/PointsTo/PointerAnalysis.h"
#include "llvm/llvm-utils.h"
#include "llvm/analysis/PointsTo/PointerSubgraph.h"
#include "llvm/analysis/PointsTo/PointerSubgraphValidator.h"
#include "analysis/PointsTo/PointerSubgraphOptimizations.h"
#include "analysis/PointsTo/PointsToWithInvalidate.h"

namespace dg {

using analysis::pta::PointerSubgraph;
using analysis::pta::PSNode;
using analysis::pta::LLVMPointerSubgraphBuilder;
using analysis::pta::PSNodesSeq;
using analysis::Offset;

template <typename PTType>
class LLVMPointerAnalysisImpl : public PTType
{
    LLVMPointerSubgraphBuilder *builder;

public:
    LLVMPointerAnalysisImpl(PointerSubgraph *PS, LLVMPointerSubgraphBuilder *b)
    : PTType(PS), builder(b) {}

    // build new subgraphs on calls via pointer
    virtual bool functionPointerCall(PSNode *callsite, PSNode *called)
    {
        // with vararg it may happen that we get pointer that
        // is not to function, so just bail out here in that case
        if (!llvm::isa<llvm::Function>(called->getUserData<llvm::Value>()))
            return false;

        const llvm::Function *F = called->getUserData<llvm::Function>();
        const llvm::CallInst *CI = callsite->getUserData<llvm::CallInst>();

        // incompatible prototypes, skip it...
        if (!llvmutils::callIsCompatible(F, CI))
            return false;

        if (F->size() == 0) {
            // calling declaration that returns a pointer?
            // That is unknown pointer
            return callsite->getPairedNode()->addPointsTo(analysis::pta::PointerUnknown);
        }

        // create new instructions
        auto cf = builder->createFuncptrCall(CI, F);
        assert(cf.first && "Failed building the subgraph");

        // we got the return site for the call stored as the paired node
        PSNode *ret = callsite->getPairedNode();
        if (cf.second) {
            // If we have some returns from this function,
            // pass the returned values to the return site.
            ret->addOperand(cf.second);
            cf.second->addSuccessor(ret);
        }

        // Connect the graph to the original graph --
        // replace the edge call->ret that we have added
        // due to the connectivity of the graph.
        // Now we know what is to be called, so we can remove it.
        // We can also replace the edge only when we know
        // that the function will return.
        // If the function does not return, we cannot trim the graph
        // here as this called function may be due to an approximation
        // and the real called function can be established in
        // the following code (if this call is on a cycle).
        if (callsite->successorsNum() == 1 &&
            callsite->getSingleSuccessor() == ret
            && cf.second) {
            callsite->replaceSingleSuccessor(cf.first);
        } else {
            // we already have some subgraph connected,
            // so just add a new one
            callsite->addSuccessor(cf.first);
        }

#ifndef NDEBUG
        // check the graph after rebuilding
        analysis::pta::debug::LLVMPointerSubgraphValidator validator(builder->getPS());
        if (validator.validate()) {
            llvm::errs() << "Pointer Subgraph is broken!\n";
            llvm::errs() << "This happend after building this function called via pointer: "
                         <<  F->getName() << "\n";
            assert(!validator.getErrors().empty());
            llvm::errs() << validator.getErrors();
            abort();
        }
#endif // NDEBUG

        return true;
    }
};

class LLVMPointerAnalysis
{
    PointerSubgraph *PS = nullptr;
    LLVMPointerSubgraphBuilder *builder;

public:

    LLVMPointerAnalysis(const llvm::Module *m, const char *entry_func = "main",
                        uint64_t field_sensitivity = Offset::UNKNOWN)
        : builder(new LLVMPointerSubgraphBuilder(m, entry_func, field_sensitivity)) {}

    ~LLVMPointerAnalysis()
    {
        delete builder;
    }

    PSNode *getPointsTo(const llvm::Value *val)
    {
        return builder->getPointsTo(val);
    }

    const std::unordered_map<const llvm::Value *, PSNodesSeq>&
    getNodesMap() const
    {
        return builder->getNodesMap();
    }

    std::vector<PSNode *> getNodes()
    {
        return PS->getNodes();
    }

    PointerSubgraph *getPS() { return PS; }
    const PointerSubgraph *getPS() const { return PS; }

    void buildSubgraph()
    {
        // run the analysis itself
        assert(builder && "Incorrectly constructed PTA, missing builder");

        PS = builder->buildLLVMPointerSubgraph();
        if (!PS) {
            llvm::errs() << "Pointer Subgraph was not built, aborting\n";
            abort();
        }

/*
        analysis::pta::PointerSubgraphOptimizer optimizer(PS);
        optimizer.run();

        if (optimizer.getNumOfRemovedNodes() > 0)
            builder->composeMapping(std::move(optimizer.getMapping()));

        llvm::errs() << "PS optimization removed " << optimizer.getNumOfRemovedNodes() << " nodes\n";

#ifndef NDEBUG
        analysis::pta::debug::LLVMPointerSubgraphValidator validator(builder->getPS());
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

    template <typename PTType>
    void run()
    {
        buildSubgraph();

        LLVMPointerAnalysisImpl<PTType> PTA(PS, builder);
        PTA.run();
    }

    // this method creates PointerAnalysis object and returns it.
    // It is alternative to run() method, but it does not delete all
    // the analysis data as the run() (like memory objects and so on).
    // run() preserves only PointerSubgraph and the builder
    template <typename PTType>
    analysis::pta::PointerAnalysis *createPTA()
    {
        buildSubgraph();
        return new LLVMPointerAnalysisImpl<PTType>(PS, builder);
    }
};

template <>
inline void LLVMPointerAnalysis::run<analysis::pta::PointsToWithInvalidate>()
{
    // build the subgraph
    assert(builder && "Incorrectly constructed PTA, missing builder");
    builder->setInvalidateNodesFlag(true);
    buildSubgraph();

    LLVMPointerAnalysisImpl<analysis::pta::PointsToWithInvalidate> PTA(PS, builder);
    PTA.run();
}

template <>
inline analysis::pta::PointerAnalysis *LLVMPointerAnalysis::createPTA<analysis::pta::PointsToWithInvalidate>()
{
    // build the subgraph
    assert(builder && "Incorrectly constructed PTA, missing builder");
    builder->setInvalidateNodesFlag(true);
    buildSubgraph();

    return new LLVMPointerAnalysisImpl<analysis::pta::PointsToWithInvalidate>(PS, builder);
}

} // namespace dg

#endif // _LLVM_DG_POINTS_TO_ANALYSIS_H_
