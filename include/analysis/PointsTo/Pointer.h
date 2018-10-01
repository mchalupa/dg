#ifndef _DG_POINTER_H_
#define _DG_POINTER_H_

#include <cassert>

#include "analysis/Offset.h"

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
    bool isInvalidated() const { return target == INVALIDATED; }
};

extern const Pointer PointerUnknown;
extern const Pointer PointerNull;

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_POINTER_H_
