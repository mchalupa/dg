#ifndef _LLVM_DG_POINTS_TO_ANALYSIS_H_
#define _LLVM_DG_POINTS_TO_ANALYSIS_H_

#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/raw_ostream.h>

#include "analysis/PSS.h"
#include "llvm/PSS.h"

namespace dg {

using analysis::pss::PSS;
using analysis::pss::PSSNode;
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

    PSSNode *getNode(const llvm::Value *val)
    {
        return builder->getNode(val);
    }

    const std::unordered_map<const llvm::Value *, PSSNode *>&
    getNodesMap() const
    {
        return builder->getNodesMap();
    }

    void getNodes(std::set<PSSNode *>& cont)
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
public:
    LLVMPointsToAnalysisImpl(const llvm::Module* M)
        : LLVMPointsToAnalysis(M)
    {
        setImpl(this);
    };

    // build new subgraphs on calls via pointer
    virtual bool functionPointerCall(PSSNode *where, PSSNode *what)
    {
        const llvm::Function *F = what->getUserData<llvm::Function>();
        const llvm::CallInst *CI = where->getUserData<llvm::CallInst>();

        std::pair<PSSNode *, PSSNode *> cf = builder->createCallToFunction(CI, F);

        // we got the return site for the call stored as the other operand
        // of the call node
        PSSNode *ret = where->getOperand(1);

        // connect the new subgraph to the graph
        // FIXME: don't do weak update, do strong update (remove the original edge
        // from call the node to return node. (There's problem with
        // inconsistent memory maps in that now)
        cf.second->addSuccessor(ret);
        where->addSuccessor(cf.first);

        return true;
    }
};

}

#endif // _LLVM_DG_POINTS_TO_ANALYSIS_H_
