/// XXX add licence

#ifndef _LLVM_NODE_H_
#define _LLVM_NODE_H_

#ifndef HAVE_LLVM
#error "Need LLVM"
#endif

#ifndef ENABLE_CFG
#error "Need CFG enabled"
#endif

#include <utility>
#include <map>
#include <set>

#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>

#include "Node.h"
#include "PointsTo.h"
#include "DefUse.h"

namespace dg {

class LLVMDependenceGraph;

typedef dg::BBlock<LLVMNode> LLVMBBlock;
typedef dg::DGParameter<LLVMNode> LLVMDGParameter;
typedef dg::DGParameters<const llvm::Value *, LLVMNode> LLVMDGParameters;

/// ------------------------------------------------------------------
//  -- LLVMNode
/// ------------------------------------------------------------------
class LLVMNode : public Node<LLVMDependenceGraph, const llvm::Value *, LLVMNode>
{
public:
    LLVMNode(const llvm::Value *val, bool owns_value = false)
        :dg::Node<LLVMDependenceGraph, const llvm::Value *, LLVMNode>(val),
         operands(nullptr), operands_num(0), memoryobj(nullptr), has_unknown_value(false),
         owns_key(owns_value), data(nullptr)
    {}

    ~LLVMNode();

    const llvm::Value *getValue() const { return getKey(); }

    // create new subgraph with actual parameters that are given
    // by call-site and add parameter edges between actual and
    // formal parameters. The argument is the graph of called function.
    // Must be called only when node is call-site.
    // XXX create new class for parameters
    void addActualParameters(LLVMDependenceGraph *);
    void addActualParameters(LLVMDependenceGraph *, const llvm::Function *);

    LLVMNode **getOperands();
    size_t getOperandsNum();
    LLVMNode *getOperand(unsigned int idx);
    LLVMNode *setOperand(LLVMNode *op, unsigned int idx);

    analysis::PointsToSetT& getPointsTo() { return pointsTo; }
    const analysis::PointsToSetT& getPointsTo() const { return pointsTo; }
    analysis::MemoryObj *&getMemoryObj() { return memoryobj; }
    analysis::MemoryObj *getMemoryObj() const { return memoryobj; }

    void dump() const;

    bool isPointerTy() const
    {
        return getKey()->getType()->isPointerTy();
    }

    bool isVoidTy() const
    {
        return getKey()->getType()->isVoidTy();
    }

    bool addPointsTo(const analysis::Pointer& vr)
    {
        return pointsTo.insert(vr).second;
    }

    bool addPointsTo(analysis::MemoryObj *m, analysis::Offset off = 0)
    {
        return pointsTo.insert(analysis::Pointer(m, off)).second;
    }

    bool hasUnknownValue() const { return has_unknown_value; }
    bool setUnknownValue()
    {
        if (has_unknown_value)
            return false;

        has_unknown_value = true;
        pointsTo.clear();

        return true;
    }

    template <typename T>
    T* getData() { return static_cast<T *>(data); }

    template <typename T>
    const T* getData() const { return static_cast<T *>(data); }

    template <typename T>
    void *setData(T *newdata)
    {
        void *old = data;
        data = static_cast<void *>(newdata);
        return old;
    }


private:
    LLVMNode **findOperands();
    // here we can store operands of instructions so that
    // finding them will be asymptotically constant
    LLVMNode **operands;
    size_t operands_num;

    analysis::MemoryObj *memoryobj;
    analysis::PointsToSetT pointsTo;
    analysis::DefMap defMap;

    bool has_unknown_value;
    bool owns_key;

    // user's or analysis's arbitrary data
    void *data;
};

} // namespace dg

#endif // _LLVM_NODE_H_
