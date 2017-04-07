#ifndef _DG_POINTER_H_
#define _DG_POINTER_H_

#include <map>
#include <set>
#include <cassert>

#include "analysis/Offset.h"

namespace dg {
namespace analysis {
namespace pta {

// declare PSNode
class PSNode;

struct MemoryObject;
struct Pointer;

extern const Pointer PointerUnknown;
extern const Pointer PointerNull;
extern PSNode *NULLPTR;
extern PSNode *UNKNOWN_MEMORY;

struct Pointer
{
    Pointer(PSNode *n, Offset off = 0) : target(n), offset(off)
    {
        assert(n && "Cannot have a pointer with nullptr as target");
    }

    // PSNode that allocated the memory this pointer points-to
    PSNode *target;
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
    bool isUnknown() const { return target == UNKNOWN_MEMORY; };
    bool isValid() const { return !isNull() && !isUnknown(); }
};

using PointsToSetT = std::set<Pointer>;
using PointsToMapT = std::map<Offset, PointsToSetT>;
using ValuesSetT = std::set<PSNode *>;
using ValuesMapT = std::map<Offset, ValuesSetT>;

struct MemoryObject
{
    MemoryObject(/*uint64_t s = 0, bool isheap = false, */PSNode *n = nullptr)
        : node(n) /*, is_heap(isheap), size(s)*/ {}

    // where was this memory allocated? for debugging
    PSNode *node;
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

    bool addPointsTo(const Offset& off, const PointsToSetT& pointers)
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

} // namespace pta
} // namespace analysis
} // namespace dg

#endif
