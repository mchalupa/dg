#ifndef _LLVM_DEF_USE_ANALYSIS_H_
#define _LLVM_DEF_USE_ANALYSIS_H_

#include "analysis/DataFlowAnalysis.h"

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>

namespace llvm {
    class DataLayout;
    class ConstantExpr;
};

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

namespace analysis {

class LLVMDefUseAnalysis : public DataFlowAnalysis<LLVMNode>
{
    LLVMDependenceGraph *dg;
    const llvm::DataLayout *DL;
public:
    LLVMDefUseAnalysis(LLVMDependenceGraph *dg);

    /* virtual */
    bool runOnNode(LLVMNode *node);
private:
    Pointer getConstantExprPointer(const llvm::ConstantExpr *);
    LLVMNode *getOperand(LLVMNode *node, const llvm::Value *val, unsigned int idx);

    void handleLoadInst(const llvm::LoadInst *, LLVMNode *);
    void handleStoreInst(const llvm::StoreInst *, LLVMNode *);
    void handleIntrinsicCall(LLVMNode *, const llvm::CallInst *);
    void handleUndefinedCall(LLVMNode *);
    void handleCallInst(LLVMNode *);
    void handleUndefinedCall(LLVMNode *, const llvm::CallInst *);

    void addIndirectDefUse(LLVMNode *, LLVMNode *, DefMap *);
    void addDefUseToOperands(LLVMNode *, LLVMDGParameters *, DefMap *);
    void addDefUseToParameterGlobals(LLVMNode *, LLVMDGParameters *, DefMap *);
};

} // namespace analysis
} // namespace dg

#endif //  _LLVM_DEF_USE_ANALYSIS_H_
