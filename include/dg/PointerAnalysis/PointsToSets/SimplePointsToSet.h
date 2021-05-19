#ifndef SIMPLEPOINTSTOSET_H
#define SIMPLEPOINTSTOSET_H

#include "dg/ADT/Bitvector.h"
#include "dg/PointerAnalysis/Pointer.h"
#include "dg/util/iterators.h"

#include <cassert>
#include <set>

namespace dg {
namespace pta {

class PSNode;
//
// We keep the implementation of this points-to set because
// it is good for comparison and regression testing
class SimplePointsToSet {
    using ContainerT = std::set<Pointer>;
    ContainerT pointers;

    bool addWithUnknownOffset(PSNode *target) {
        if (has({target, Offset::UNKNOWN}))
            return false;

        ContainerT tmp;
        for (const auto &ptr : pointers) {
            if (ptr.target != target)
                tmp.insert(ptr);
        }

        tmp.swap(pointers);
        return pointers.insert({target, Offset::UNKNOWN}).second;
    }

  public:
    SimplePointsToSet() = default;
    SimplePointsToSet(std::initializer_list<Pointer> elems) { add(elems); }

    using const_iterator = typename ContainerT::const_iterator;

    bool add(PSNode *target, Offset off) {
        if (off.isUnknown())
            return addWithUnknownOffset(target);

        // if we have the same pointer but with unknown offset,
        // do nothing
        if (has({target, Offset::UNKNOWN}))
            return false;

        return pointers.emplace(target, off).second;
    }

    bool add(const Pointer &ptr) { return add(ptr.target, ptr.offset); }

    // make union of the two sets and store it
    // into 'this' set (i.e. merge rhs to this set)
    bool add(const SimplePointsToSet &rhs) {
        bool changed = false;
        for (const auto &ptr : rhs.pointers) {
            changed |= pointers.insert(ptr).second;
        }

        return changed;
    }

    bool add(std::initializer_list<Pointer> elems) {
        bool changed = false;
        for (const auto &e : elems) {
            changed |= add(e);
        }
        return changed;
    }

    bool remove(const Pointer &ptr) { return pointers.erase(ptr) != 0; }

    ///
    // Remove pointer to this target with this offset.
    // This is method really removes the pair
    // (target, off), even when the off is unknown
    bool remove(PSNode *target, Offset offset) {
        return remove(Pointer(target, offset));
    }

    ///
    // Remove pointers pointing to this target
    bool removeAny(PSNode *target) {
        if (pointsToTarget(target)) {
            SimplePointsToSet tmp;
            for (const auto &ptr : pointers) {
                if (ptr.target == target) {
                    continue;
                }
                tmp.add(ptr);
            }
            assert(tmp.size() < size());
            swap(tmp);
            return true;
        }
        return false;
    }

    void clear() { pointers.clear(); }

    bool pointsTo(const Pointer &ptr) const { return pointers.count(ptr) > 0; }

    // points to the pointer or the the same target
    // with unknown offset? Note: we do not count
    // unknown memory here...
    bool mayPointTo(const Pointer &ptr) const {
        return pointsTo(ptr) || pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
    }

    bool mustPointTo(const Pointer &ptr) const {
        assert(!ptr.offset.isUnknown() && "Makes no sense");
        return pointsTo(ptr) && isSingleton();
    }

    bool pointsToTarget(PSNode *target) const {
        return dg::any_of(pointers, [target](const Pointer &ptr) {
            return ptr.target == target;
        });
    }

    bool isSingleton() const { return pointers.size() == 1; }

    size_t count(const Pointer &ptr) { return pointers.count(ptr); }
    size_t size() const { return pointers.size(); }
    bool empty() const { return pointers.empty(); }
    bool has(const Pointer &ptr) { return count(ptr) > 0; }
    bool hasUnknown() const { return pointsToTarget(UNKNOWN_MEMORY); }
    bool hasNull() const { return pointsToTarget(NULLPTR); }
    bool hasInvalidated() const { return pointsToTarget(INVALIDATED); }

    void swap(SimplePointsToSet &rhs) { pointers.swap(rhs.pointers); }

    const_iterator begin() const { return pointers.begin(); }
    const_iterator end() const { return pointers.end(); }
};

} // namespace pta
} // namespace dg

#endif /* SIMPLEPOINTSTOSET_H */
