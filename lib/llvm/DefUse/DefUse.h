#ifndef LLVM_DEF_USE_ANALYSIS_H_
#define LLVM_DEF_USE_ANALYSIS_H_

#include <vector>

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>

#include "dg/legacy/DataFlowAnalysis.h"
#include "dg/llvm/DataDependence/DataDependence.h"

using dg::dda::LLVMDataDependenceAnalysis;

namespace llvm {
class DataLayout;
class ConstantExpr;
}; // namespace llvm

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

class LLVMDefUseAnalysis : public legacy::DataFlowAnalysis<LLVMNode> {
    LLVMDependenceGraph *dg;
    LLVMDataDependenceAnalysis *RD;
    LLVMPointerAnalysis *PTA;
    const llvm::DataLayout *DL;

  public:
    LLVMDefUseAnalysis(LLVMDependenceGraph *dg, LLVMDataDependenceAnalysis *rd,
                       LLVMPointerAnalysis *pta);

    ~LLVMDefUseAnalysis() { delete DL; }

    /* virtual */
    bool runOnNode(LLVMNode *node, LLVMNode *prev) override;

  private:
    void addDataDependencies(LLVMNode *node);

    void handleLoadInst(llvm::LoadInst *, LLVMNode *);
    void handleCallInst(LLVMNode *);
    void handleInlineAsm(LLVMNode *callNode);
    void handleIntrinsicCall(LLVMNode *callNode, llvm::CallInst *CI);
    void handleUndefinedCall(LLVMNode *callNode, llvm::CallInst *CI);
};

} // namespace dg

#endif //  LLVM_DEF_USE_ANALYSIS_H_
