#ifndef _LLVM_REACHING_DEFS_ANALYSIS_H_
#define _LLVM_REACHING_DEFS_ANALYSIS_H_

#include "analysis/DataFlowAnalysis.h"
#include "DefMap.h"

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DataLayout.h>

namespace llvm {
    class DataLayout;
    class ConstantExpr;
};

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

namespace analysis {

class LLVMReachingDefsAnalysis : public DataFlowAnalysis<LLVMNode>
{
    LLVMDependenceGraph *dg;
    const llvm::DataLayout *DL;
public:
    LLVMReachingDefsAnalysis(LLVMDependenceGraph *dg);
    ~LLVMReachingDefsAnalysis()
    {
        delete DL;
    }

    /* virtual */
    bool runOnNode(LLVMNode *node, LLVMNode *pred);
    void handleNode(LLVMNode *);
private:
    Pointer getConstantExprPointer(llvm::ConstantExpr *);
    LLVMNode *getOperand(LLVMNode *node,
                         llvm::Value *val, unsigned int idx);

    bool handleCallInst(LLVMDependenceGraph *, LLVMNode *, DefMap *);
    bool handleStoreInst(LLVMNode *, DefMap *, PointsToSetT *&strong_update);

    bool handleUndefinedCall(LLVMNode *, llvm::CallInst *, DefMap *);
    bool handleIntrinsicCall(LLVMNode *, llvm::CallInst *, DefMap *);
    bool handleUndefinedCall(LLVMNode *, DefMap *);
    bool handleCallInst(LLVMNode *, DefMap *);
};

} // namespace analysis
} // namespace dg

#endif
