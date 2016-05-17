#ifndef _LLVM_DG_POINTS_TO_ANALYSIS_H_
#define _LLVM_DG_POINTS_TO_ANALYSIS_H_

#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/raw_ostream.h>

#include "analysis/PointsTo/PSS.h"
#include "llvm/analysis/PSS.h"

namespace dg {

using analysis::pss::PSS;
using analysis::pss::PSNode;
using analysis::pss::LLVMPSSBuilder;

class LLVMPointsToAnalysis
{
protected:
    PSS *impl;
    LLVMPSSBuilder *builder;
    LLVMPointsToAnalysis(const llvm::Module *M)
        :builder(new LLVMPSSBuilder(M)) {}

    // the real analysis that will run
    void setImpl(PSS *im) { impl = im; }

public:
    LLVMPointsToAnalysis(PSS *p) : impl(p) {};
    ~LLVMPointsToAnalysis() { delete builder; }

    PSNode *getNode(const llvm::Value *val)
    {
        return builder->getNode(val);
    }

    PSNode *getPointsTo(const llvm::Value *val)
    {
        return builder->getPointsTo(val);
    }

    const std::unordered_map<const llvm::Value *, PSNode *>&
    getNodesMap() const
    {
        return builder->getNodesMap();
    }

    void getNodes(std::set<PSNode *>& cont)
    {
        impl->getNodes(cont);
    }

    void run()
    {
        impl->setRoot(builder->buildLLVMPSS());
        impl->run();
    }
};

template <typename PTType>
class LLVMPointsToAnalysisImpl : public PTType, public LLVMPointsToAnalysis
{
    bool callIsCompatible(const llvm::Function *F, const llvm::CallInst *CI)
    {
        using namespace llvm;

        if (F->arg_size() > CI->getNumArgOperands())
            return false;

        int idx = 0;
        for (auto A = F->arg_begin(), E = F->arg_end(); A != E; ++A, ++idx) {
            Type *CTy = CI->getArgOperand(idx)->getType();
            Type *ATy = A->getType();

            if (!CTy->canLosslesslyBitCastTo(ATy))
                return false;
        }

        return true;
    }

public:
    LLVMPointsToAnalysisImpl(const llvm::Module* M)
        : LLVMPointsToAnalysis(M)
    {
        setImpl(this);
    };

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
        if (!callIsCompatible(F, CI))
            return false;

        if (F->size() == 0) {
            // calling declaration that returns a pointer?
            // That is unknown pointer
            return callsite->getPairedNode()->addPointsTo(analysis::pss::PointerUnknown);
        }

        std::pair<PSNode *, PSNode *> cf = builder->createCallToFunction(CI, F);
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
        // in PSS. Don't do anything in this case
        if (!val->getType()->isPointerTy())
            return false;

        llvm::errs() << "PTA - error @ " << *val << " : no points-to >>\n\t"
                     << *val2 << "\n";

        return false;
    }
    */
};

}

#endif // _LLVM_DG_POINTS_TO_ANALYSIS_H_
