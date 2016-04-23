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
typedef dg::DGParameters<LLVMNode> LLVMDGParameters;

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
    bool runOnNode(LLVMNode *node, LLVMNode *prev);

private:
    Pointer getConstantExprPointer(llvm::ConstantExpr *);
    LLVMNode *getOperand(LLVMNode *, llvm::Value *, unsigned int);
    bool addGlobalPointsTo(llvm::Constant *, LLVMNode *, uint64_t);
    bool propagatePointersToArguments(LLVMDependenceGraph *,
                                      const llvm::CallInst *, LLVMNode *);
    bool propagatePointersFromArguments(LLVMDependenceGraph *, LLVMNode *);

    bool handleFunctionPtrCall(LLVMNode *calledFuncNode, LLVMNode *node);
    void addDynamicCallersParamsPointsTo(LLVMNode *, LLVMDependenceGraph *);
    bool handleLoadInstPtr(const Pointer&, LLVMNode *);
    bool handleLoadInstPointsTo(LLVMNode *, LLVMNode *);

    bool handleAllocaInst(LLVMNode *);
    bool handleStoreInst(llvm::StoreInst *, LLVMNode *);
    bool handleLoadInst(llvm::LoadInst *, LLVMNode *);
    bool handleGepInst(llvm::GetElementPtrInst *, LLVMNode *);
    bool handleCallInst(llvm::CallInst *, LLVMNode *);
    bool handleIntrinsicFunction(llvm::CallInst *, LLVMNode *);
    bool handleIntToPtr(llvm::IntToPtrInst *, LLVMNode *);
    bool handleBitCastInst(llvm::BitCastInst *, LLVMNode *);
    bool handleReturnInst(llvm::ReturnInst *, LLVMNode *);
    bool handlePHINode(llvm::PHINode *, LLVMNode *);
    bool handleSelectNode(llvm::SelectInst *, LLVMNode *);

    bool handleMemTransfer(llvm::IntrinsicInst *, LLVMNode *);
    void propagateVarArgPointsTo(LLVMDGParameters *, size_t, LLVMNode *);
};

} // namespace analysis
} // namespace dg

#endif //  _LLVM_POINTS_TO_ANALYSIS_H_
