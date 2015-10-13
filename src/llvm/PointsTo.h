#ifndef _LLVM_POINTS_TO_ANALYSIS_H_
#define _LLVM_POINTS_TO_ANALYSIS_H_

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DataLayout.h>

#include "analysis/DataFlowAnalysis.h"
#include "AnalysisGeneric.h"

namespace llvm {
    class IntrinsicInst;
}

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

namespace analysis {

class LLVMPointsToAnalysis : public DataFlowAnalysis<LLVMNode>
{
    LLVMDependenceGraph *dg;
    void handleGlobals();
    const llvm::DataLayout *DL;
public:
    LLVMPointsToAnalysis(LLVMDependenceGraph *);
    ~LLVMPointsToAnalysis()
    {
        delete DL;
    }

    /* virtual */
    bool runOnNode(LLVMNode *node);

private:
    Pointer getConstantExprPointer(const llvm::ConstantExpr *);
    LLVMNode *getOperand(LLVMNode *, const llvm::Value *, unsigned int);
    bool addGlobalPointsTo(const llvm::Constant *, LLVMNode *, uint64_t);
    bool propagatePointersToArguments(LLVMDependenceGraph *,
                                      const llvm::CallInst *, LLVMNode *);

    bool handleFunctionPtrCall(LLVMNode *calledFuncNode, LLVMNode *node);
    void addDynamicCallersParamsPointsTo(LLVMNode *, LLVMDependenceGraph *);

    bool handleAllocaInst(LLVMNode *);
    bool handleStoreInst(const llvm::StoreInst *, LLVMNode *);
    bool handleLoadInst(const llvm::LoadInst *, LLVMNode *);
    bool handleGepInst(const llvm::GetElementPtrInst *, LLVMNode *);
    bool handleCallInst(const llvm::CallInst *, LLVMNode *);
    bool handleIntrinsicFunction(const llvm::CallInst *, LLVMNode *);
    bool handleIntToPtr(const llvm::IntToPtrInst *, LLVMNode *);
    bool handleBitCastInst(const llvm::BitCastInst *, LLVMNode *);
    bool handleReturnInst(const llvm::ReturnInst *, LLVMNode *);
    bool handlePHINode(const llvm::PHINode *, LLVMNode *);
    bool handleMemTransfer(const llvm::IntrinsicInst *, LLVMNode *);
    void propagateVarArgPointsTo(LLVMDGParameters *, size_t, LLVMNode *);
};

} // namespace analysis
} // namespace dg

#endif //  _LLVM_POINTS_TO_ANALYSIS_H_
