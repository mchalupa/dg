#ifndef _LLVM_ANALYSIS_GENERIC_H_
#define _LLVM_ANALYSIS_GENERIC_H_

#include <map>
#include <set>
#include <assert.h>

#include "analysis/Offset.h"

namespace llvm {
    class Value;
    class ConstantExpr;
    class DataLayout;
};

namespace dg {

class LLVMDependenceGraph;
class LLVMNode;

namespace analysis {

struct MemoryObj;
struct Pointer
{
    Pointer(MemoryObj *m, Offset off = 0) : obj(m), offset(off)
    {
        assert(m && "Cannot have a pointer with nullptr as memory object");
    }

    MemoryObj *obj;
    Offset offset;

    bool operator<(const Pointer& oth) const
    {
        return obj == oth.obj ? offset < oth.offset : obj < oth.obj;
    }

    bool isUnknown() const;
    bool pointsToUnknown() const;
    bool isKnown() const;
    bool isNull() const;
    bool pointsToHeap() const;
};

typedef std::set<Pointer> PointsToSetT;
typedef std::set<LLVMNode *> ValuesSetT;
typedef std::map<Offset, PointsToSetT> PointsToMapT;
typedef std::map<Offset, ValuesSetT> ValuesMapT;

struct MemoryObj
{
    MemoryObj(LLVMNode *n, uint64_t s = 0, bool isheap = false)
        : node(n), is_heap(isheap), size(s) {}

    LLVMNode *node;
    PointsToMapT pointsTo;
    // some analyses need to know if this is heap or stack
    // allocated object
    bool is_heap;

    bool addPointsTo(const Offset& off, const Pointer& ptr)
    {
        if (isUnknown())
            return false;

        assert(ptr.obj != nullptr && "Cannot have NULL object, use unknown instead");
        return pointsTo[off].insert(ptr).second;
    }

    bool addPointsTo(const Offset& off, const std::set<Pointer>& pointers)
    {
        if (isUnknown())
            return false;

        bool changed = false;
        for (const Pointer& ptr : pointers)
            changed |= pointsTo[off].insert(ptr).second;

        return changed;
    }

    bool isUnknown() const;
    bool isNull() const;
    bool isHeapAllocated() const { return is_heap; }

    // if the object is allocated via malloc or
    // similar, we can not infer the size from type,
    // because it is recast to (usually) i8*. Store the
    // size information here, if applicable and available
    uint64_t size;

    bool hasSize() const { return size != 0; }
#if 0
    ValuesMapT values;
    // actually we should take offsets into
    // account, since we can memset only part of the memory
    ValuesSetT memsetTo;
#endif
};

extern MemoryObj UnknownMemoryObject;
extern MemoryObj NullMemoryObject;
extern Pointer UnknownMemoryLocation;
extern Pointer NullPointer;

Pointer getConstantExprPointer(llvm::ConstantExpr *CE,
                               LLVMDependenceGraph *dg,
                               const llvm::DataLayout *DL);

LLVMNode *getOperand(LLVMNode *node, llvm::Value *val,
                     unsigned int idx, const llvm::DataLayout *DL);

} // namespace analysis
} // namespace dg

#endif // _LLVM_ANALYSIS_GENERIC_H_
