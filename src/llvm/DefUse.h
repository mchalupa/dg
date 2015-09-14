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
    LLVMNode *getOperand(LLVMNode *node, const llvm::Value *val, unsigned int idx);

    void handleLoadInst(const llvm::LoadInst *, LLVMNode *);
    void handleStoreInst(const llvm::StoreInst *, LLVMNode *);
};

class DefMap
{
    // last definition of memory location
    // pointed to by the Pointer
    typedef std::map<Pointer, ValuesSetT>::iterator iterator;
    typedef std::map<Pointer, ValuesSetT>::const_iterator const_iterator;
    std::map<Pointer, ValuesSetT> defs;

public:
    DefMap() {}
    DefMap(const DefMap& o);

    bool merge(const DefMap *o, PointsToSetT *without = nullptr);
    bool add(const Pointer& p, LLVMNode *n);
    bool update(const Pointer& p, LLVMNode *n);

    iterator begin() { return defs.begin(); }
    iterator end() { return defs.end(); }
    const_iterator begin() const { return defs.begin(); }
    const_iterator end() const { return defs.end(); }

    ValuesSetT& get(const Pointer& ptr) { return defs[ptr]; }
    const std::map<Pointer, ValuesSetT> getDefs() const { return defs; }
};

} // namespace analysis
} // namespace dg

#endif //  _LLVM_DEF_USE_ANALYSIS_H_
