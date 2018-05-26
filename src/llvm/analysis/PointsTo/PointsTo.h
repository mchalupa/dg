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
        std::pair<PSNode *, PSNode *> cf = builder->createFuncptrCall(CI, F);
        assert(cf.first && cf.second);

        // we got the return site for the call stored as the paired node
        PSNode *ret = callsite->getPairedNode();
        // ret is a PHI node, so pass the values returned from the
        // procedure call
        ret->addOperand(cf.second);

        // replace the edge from call->ret that we
        // have due to connectivity of the graph until we
        // insert the subgraph
        if (callsite->successorsNum() == 1 &&
            callsite->getSingleSuccessor() == ret) {
            callsite->replaceSingleSuccessor(cf.first);
        } else
            callsite->addSuccessor(cf.first);

        cf.second->addSuccessor(ret);

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

    /*
    virtual bool error(PSNode *at, const char *msg)
    {
        llvm::Value *val = at->getUserData<llvm::Value>();
        assert(val);

        llvm::errs() << "PTA - error @ " << *val << " : " << msg << "\n";
        return false;
    }

    virtual bool errorEmptyPointsTo(PSNode *from, PSNode *to)
    {
        // this is valid even in flow-insensitive points-to
        // because we process the nodes in CFG order
        llvm::Value *val = from->getUserData<llvm::Value>();
        assert(val);
        llvm::Value *val2 = to->getUserData<llvm::Value>();
        assert(val2);

        // due to int2ptr we may have spurious loads
        // in PointerSubgraph. Don't do anything in this case
        if (!val->getType()->isPointerTy())
            return false;

        llvm::errs() << "PTA - error @ " << *val << " : no points-to >>\n\t"
                     << *val2 << "\n";

        return false;
    }
    */
};

class LLVMPointerAnalysis
{
    PointerSubgraph *PS = nullptr;
    LLVMPointerSubgraphBuilder *builder;

public:

    LLVMPointerAnalysis(const llvm::Module *m,
                        uint64_t field_sensitivity = Offset::UNKNOWN)
        : builder(new LLVMPointerSubgraphBuilder(m, field_sensitivity)) {}

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
