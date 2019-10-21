#ifndef _LLVM_DEF_USE_ANALYSIS_H_
#define _LLVM_DEF_USE_ANALYSIS_H_

#include <vector>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DataLayout.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/analysis/DataDependence/DataDependence.h"

using dg::analysis::LLVMDataDependenceAnalysis;

namespace llvm {
    class DataLayout;
    class ConstantExpr;
};

namespace dg {
namespace legacy {

class LLVMDependenceGraph;
class LLVMNode;

class LLVMDefUseAnalysis {
    LLVMDependenceGraph *dg;
    LLVMDataDependenceAnalysis *DDA;
    LLVMPointerAnalysis *PTA;

public:
    LLVMDefUseAnalysis(LLVMDependenceGraph *dg,
                       LLVMDataDependenceAnalysis *rd,
                       LLVMPointerAnalysis *pta);

    void run();
private:
    void runOnNode(LLVMNode *node);

    void addDataDependence(LLVMNode *node,
                           const std::vector<llvm::Value *>& defs);

    void addDataDependence(LLVMNode *node, llvm::Value *val);

    void handleLoadInst(llvm::LoadInst *, LLVMNode *);
    void handleCallInst(LLVMNode *);
    void handleInlineAsm(LLVMNode *callNode);
    void handleIntrinsicCall(LLVMNode *callNode, llvm::CallInst *CI);
    void handleUndefinedCall(LLVMNode *callNode, llvm::CallInst *CI);
};

} // namespace legacy
} // namespace dg

#endif //  _LLVM_DEF_USE_ANALYSIS_H_
