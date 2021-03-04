#ifndef _LLVM_DEF_USE_ANALYSIS_H_
#define _LLVM_DEF_USE_ANALYSIS_H_

#include <vector>

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DataLayout.h>
SILENCE_LLVM_WARNINGS_POP

#include "dg/legacy/DataFlowAnalysis.h"
#include "dg/llvm/DataDependence/DataDependence.h"

using dg::dda::LLVMDataDependenceAnalysis;

namespace llvm {
    class DataLayout;
    class ConstantExpr;
};

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

class LLVMDefUseAnalysis : public legacy::DataFlowAnalysis<LLVMNode>
{
    LLVMDependenceGraph *dg;
    LLVMDataDependenceAnalysis *RD;
    LLVMPointerAnalysis *PTA;
    const llvm::DataLayout *DL;

public:
    LLVMDefUseAnalysis(LLVMDependenceGraph *dg,
                       LLVMDataDependenceAnalysis *rd,
                       LLVMPointerAnalysis *pta);

    ~LLVMDefUseAnalysis() { delete DL; }

    /* virtual */
    bool runOnNode(LLVMNode *node, LLVMNode *prev);
private:
    void addDataDependencies(LLVMNode *node);

    void handleLoadInst(llvm::LoadInst *, LLVMNode *);
    void handleCallInst(LLVMNode *);
    void handleInlineAsm(LLVMNode *callNode);
    void handleIntrinsicCall(LLVMNode *callNode, llvm::CallInst *CI);
    void handleUndefinedCall(LLVMNode *callNode, llvm::CallInst *CI);
};

} // namespace dg

#endif //  _LLVM_DEF_USE_ANALYSIS_H_
