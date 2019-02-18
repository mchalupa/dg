#ifndef _DG_POINTER_H_
#define _DG_POINTER_H_

#include "dg/analysis/Offset.h"
#include <cassert>

namespace dg {
namespace analysis {
namespace pta {

// declare PSNode
class PSNode;

extern PSNode *NULLPTR;
extern PSNode *UNKNOWN_MEMORY;
extern PSNode *INVALIDATED;

struct Pointer
{
    Pointer(PSNode *n, Offset off) : target(n), offset(off)
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
    bool isUnknown() const { return target == UNKNOWN_MEMORY; }
    bool isValid() const { return !isNull() && !isUnknown(); }
    bool isInvalidated() const { return target == INVALIDATED; }

#ifndef NDEBUG
    void dump() const;
    void print() const;
#endif // not NDEBUG


};

extern const Pointer UnknownPointer;
extern const Pointer NullPointer;

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_POINTER_H_
