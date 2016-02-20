#ifndef _DG_POINTER_H_
#define _DG_POINTER_H_

#include <map>
#include <set>
#include <cassert>

namespace dg {
namespace analysis {

// declare PSSNode
class PSSNode;

#define UNKNOWN_OFFSET ~((uint64_t) 0)

// just a wrapper around uint64_t to
// handle UNKNOWN_OFFSET somehow easily
// maybe later we'll make it a range
struct Offset
{
    Offset(uint64_t o = UNKNOWN_OFFSET) : offset(o) {}
    Offset& operator+=(const Offset& o)
    {
        if (offset == UNKNOWN_OFFSET)
            return *this;

        if (o.offset == UNKNOWN_OFFSET)
            offset = UNKNOWN_OFFSET;
        else
            offset += o.offset;

        return *this;
    }

    Offset operator+(const Offset& o)
    {
        if (offset == UNKNOWN_OFFSET || o.offset == UNKNOWN_OFFSET)
            return UNKNOWN_OFFSET;

        return Offset(offset + o.offset);
    }

    bool operator<(const Offset& o) const
    {
        return offset < o.offset;
    }

    bool operator==(const Offset& o) const
    {
        return offset == o.offset;
    }

    bool inRange(uint64_t from, uint64_t to) const
    {
        return (offset >= from && offset <= to);
    }

    bool isUnknown() const { return offset == UNKNOWN_OFFSET; }

    uint64_t operator*() const { return offset; }
    const uint64_t *operator->() const { return &offset; }

    uint64_t offset;
};

struct MemoryObject;
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

#if 0
    bool isUnknown() const;
    bool pointsToUnknown() const;
    bool isKnown() const;
    bool isNull() const;
    bool pointsToHeap() const;
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

} // namespace analysis
} // namespace dg

#endif
