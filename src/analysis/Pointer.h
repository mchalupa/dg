#ifndef _DG_POINTER_H_
#define _DG_POINTER_H_

#include <map>
#include <set>
#include <cassert>

#include "Offset.h"

namespace dg {
namespace analysis {
namespace pss {

// declare PSSNode
class PSSNode;

struct MemoryObject;
struct Pointer;

extern const Pointer PointerUnknown;
extern const Pointer PointerNull;
extern PSSNode *NULLPTR;
extern PSSNode *UNKNOWN_MEMORY;

struct Pointer
{
    Pointer(PSSNode *n, Offset off = 0) : target(n), offset(off)
    {
        assert(n && "Cannot have a pointer with nullptr as target");
    }

    // PSSNode that allocated the memory this pointer points-to
    PSSNode *target;
    // offset into the memory it points to
    Offset offset;

    bool operator<(const Pointer& oth) const
    {
        return target == oth.target ? offset < oth.offset : target < oth.target;
    }

    bool operator==(const Pointer& oth) const
    {
        return target == oth.target && offset == oth.offset;
    }

    bool isNull() const { return target == NULLPTR; }
    bool pointsToUnknownMemory() const { return target == UNKNOWN_MEMORY; }

#if 0
    bool isUnknown() const;
    bool pointsToUnknown() const;
    bool isKnown() const;
#endif
};

typedef std::set<Pointer> PointsToSetT;
typedef std::map<Offset, PointsToSetT> PointsToMapT;
typedef std::set<PSSNode *> ValuesSetT;
typedef std::map<Offset, ValuesSetT> ValuesMapT;

struct MemoryObject
{
    MemoryObject(/*uint64_t s = 0, bool isheap = false, */PSSNode *n = nullptr)
        : node(n) /*, is_heap(isheap), size(s)*/ {}

    // where was this memory allocated? for debugging
    PSSNode *node;
    // possible pointers stored in this memory object
    PointsToMapT pointsTo;

    PointsToSetT& getPointsTo(const Offset& off) { return pointsTo[off]; }

    bool addPointsTo(const Offset& off, const Pointer& ptr)
    {
        /*
        if (isUnknown())
            return false;
            */

        assert(ptr.target != nullptr
               && "Cannot have NULL target, use unknown instead");

        return pointsTo[off].insert(ptr).second;
    }

    bool addPointsTo(const Offset& off, const std::set<Pointer>& pointers)
    {
        /*
        if (isUnknown())
            return false;
            */

        bool changed = false;

        for (const Pointer& ptr : pointers)
            changed |= addPointsTo(off, ptr);

        return changed;
    }


#if 0
    // some analyses need to know if this is heap or stack
    // allocated object
    bool is_heap;
    // if the object is allocated via malloc or
    // similar, we can not infer the size from type,
    // because it is recast to (usually) i8*. Store the
    // size information here, if applicable and available
    uint64_t size;

    bool isUnknown() const;
    bool isNull() const;
    bool isHeapAllocated() const { return is_heap; }
    bool hasSize() const { return size != 0; }
#endif
};

#if 0
extern MemoryObject UnknownMemoryObject;
extern MemoryObject NullMemoryObject;
extern Pointer UnknownMemoryLocation;
extern Pointer NullPointer;
#endif

} // namespace pss
} // namespace analysis
} // namespace dg

#endif
