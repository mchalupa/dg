#ifndef _LLVM_DEF_USE_ANALYSIS_H_
#define _LLVM_DEF_USE_ANALYSIS_H_

#include "analysis/DataFlowAnalysis.h"

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/DataLayout.h>

#include "LLVMReachingDefinitions.h"

using dg::analysis::rd::LLVMReachingDefinitions;

namespace llvm {
    class DataLayout;
    class ConstantExpr;
};

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

class LLVMDefUseAnalysis : public analysis::DataFlowAnalysis<LLVMNode>
{
    LLVMDependenceGraph *dg;
    LLVMReachingDefinitions *RD;
    LLVMPointsToAnalysis *PTA;
    const llvm::DataLayout *DL;
public:
    LLVMDefUseAnalysis(LLVMDependenceGraph *dg,
                       LLVMReachingDefinitions *rd,
                       LLVMPointsToAnalysis *pta);
    ~LLVMDefUseAnalysis() { delete DL; }

    /* virtual */
    bool runOnNode(LLVMNode *node, LLVMNode *prev);
private:
    void addDataDependence(LLVMNode *node,
                           analysis::pss::PSSNode *pts,
                           analysis::rd::RDNode *mem,
                           uint64_t size);

    void addDataDependence(LLVMNode *node,
                           const llvm::Value *where, /* in CFG */
                           const llvm::Value *ptrOp,
                           uint64_t size);

    void handleLoadInst(const llvm::LoadInst *, LLVMNode *);
    void handleCallInst(LLVMNode *);
    void handleInlineAsm(LLVMNode *callNode);
    void handleIntrinsicCall(LLVMNode *callNode, const llvm::CallInst *CI);
    /*
    void handleStoreInst(const llvm::StoreInst *, LLVMNode *);
    void handleIntrinsicCall(LLVMNode *, const llvm::CallInst *);
    void handleUndefinedCall(LLVMNode *);
    void handleUndefinedCall(LLVMNode *, const llvm::CallInst *);

    void addStoreLoadInstDefUse(LLVMNode *, LLVMNode *, DefMap *);
    void addIndirectDefUse(LLVMNode *, LLVMNode *, DefMap *);
    void addDefUseToOperands(LLVMNode *, bool, LLVMDGParameters *, DefMap *);
    void addDefUseToParameterGlobals(LLVMNode *, LLVMDGParameters *, DefMap *);
    void addIndirectDefUsePtr(const Pointer&, LLVMNode *, DefMap *, uint64_t);
    void addDefUseToParam(LLVMNode *, DefMap *, LLVMDGParameter *);
    void addDefUseToParamNode(LLVMNode *op, DefMap *df, LLVMNode *to);
    void addInitialDefuse(LLVMDependenceGraph *, ValuesSetT&, const Pointer&, uint64_t);
    */
};

namespace analysis {
namespace old {

class LLVMDefUseAnalysis : public DataFlowAnalysis<LLVMNode>
{
    LLVMDependenceGraph *dg;
    const llvm::DataLayout *DL;
public:
    LLVMDefUseAnalysis(LLVMDependenceGraph *dg);
    ~LLVMDefUseAnalysis()
    {
        delete DL;
    }

    /* virtual */
    bool runOnNode(LLVMNode *node, LLVMNode *prev);
private:
    Pointer getConstantExprPointer(const llvm::ConstantExpr *);
    LLVMNode *getOperand(LLVMNode *node, const llvm::Value *val, unsigned int idx);

    void handleLoadInst(const llvm::LoadInst *, LLVMNode *);
    void handleStoreInst(const llvm::StoreInst *, LLVMNode *);
    void handleIntrinsicCall(LLVMNode *, const llvm::CallInst *);
    void handleUndefinedCall(LLVMNode *);
    void handleCallInst(LLVMNode *);
    void handleUndefinedCall(LLVMNode *, const llvm::CallInst *);
    void handleInlineAsm(LLVMNode *callNode);

    void addStoreLoadInstDefUse(LLVMNode *, LLVMNode *, DefMap *);
    void addIndirectDefUse(LLVMNode *, LLVMNode *, DefMap *);
    void addDefUseToOperands(LLVMNode *, bool, LLVMDGParameters *, DefMap *);
    void addDefUseToParameterGlobals(LLVMNode *, LLVMDGParameters *, DefMap *);
    void addIndirectDefUsePtr(const Pointer&, LLVMNode *, DefMap *, uint64_t);
    void addDefUseToParam(LLVMNode *, DefMap *, LLVMDGParameter *);
    void addDefUseToParamNode(LLVMNode *op, DefMap *df, LLVMNode *to);
    void addInitialDefuse(LLVMDependenceGraph *, ValuesSetT&, const Pointer&, uint64_t);
};

} // namespace old
} // namespace analysis
} // namespace dg

#endif //  _LLVM_DEF_USE_ANALYSIS_H_
