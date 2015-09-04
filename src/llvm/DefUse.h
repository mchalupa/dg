#ifndef _LLVM_DEF_USE_ANALYSIS_H_
#define _LLVM_DEF_USE_ANALYSIS_H_

#include "analysis/DataFlowAnalysis.h"
#include "PointsTo.h"

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
    void addDefUseEdges();

    void handleNode(LLVMNode *);
private:
    Pointer getConstantExprPointer(const llvm::ConstantExpr *);

    void handleLoadInst(const llvm::LoadInst *, LLVMNode *);
};

class DefMap
{
    // last definition of memory location
    // pointed to by the Pointer
    std::map<Pointer, ValuesSetT> defs;

public:
    DefMap() {}
    DefMap(const DefMap& o);

    bool merge(const DefMap *o, PointsToSetT *without = nullptr);
    bool add(const Pointer& p, LLVMNode *n);
    bool update(const Pointer& p, LLVMNode *n);

    ValuesSetT& get(const Pointer& ptr) { return defs[ptr]; }
    const std::map<Pointer, ValuesSetT> getDefs() const { return defs; }
};

} // namespace analysis
} // namespace dg

#endif //  _LLVM_DEF_USE_ANALYSIS_H_
