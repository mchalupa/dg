#ifndef DG_POINTER_H_
#define DG_POINTER_H_

#include "dg/Offset.h"
#include <cassert>
#include <cstddef>

namespace dg {
namespace pta {

// declare PSNode
class PSNode;

extern PSNode *NULLPTR;
extern PSNode *UNKNOWN_MEMORY;
extern PSNode *INVALIDATED;

struct Pointer {
    Pointer() = default;

    Pointer(PSNode *n, Offset off) : target(n), offset(off) {
        assert(n && "Cannot have a pointer with nullptr as target");
    }

    // PSNode that allocated the memory this pointer points-to
    PSNode *target{nullptr};
    // offset into the memory it points to
    Offset offset;

    bool operator<(const Pointer &oth) const {
        return target == oth.target ? offset < oth.offset : target < oth.target;
    }

    bool operator==(const Pointer &oth) const {
        return target == oth.target && offset == oth.offset;
    }

    bool isNull() const { return target == NULLPTR; }
    bool isUnknown() const { return target == UNKNOWN_MEMORY; }
    bool isValid() const { return !isNull() && !isUnknown(); }
    bool isInvalidated() const { return target == INVALIDATED; }

    size_t hash() const;

#ifndef NDEBUG
    void dump() const;
    void print() const;
#endif // not NDEBUG
};

extern const Pointer UnknownPointer;
extern const Pointer NullPointer;

} // namespace pta
} // namespace dg

namespace std {
template <>
struct hash<dg::pta::Pointer> {
    size_t operator()(const dg::pta::Pointer &p) const { return p.hash(); }
};
} // namespace std

#endif
