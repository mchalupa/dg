#ifndef DG_SINGLEBITVECTORPOINTSTOSET_H
#define DG_SINGLEBITVECTORPOINTSTOSET_H

#include <cassert>
#include <map>
#include <vector>

#include "LookupTable.h"
#include "dg/ADT/Bitvector.h"
#include "dg/PointerAnalysis/Pointer.h"

namespace dg {
namespace pta {

class PSNode;

class PointerIdPointsToSet {
    static PointerIDLookupTable lookupTable;

#if defined(HAVE_TSL_HOPSCOTCH) || (__clang__)
    using PointersT = ADT::SparseBitvectorHashImpl;
#else
    using PointersT = ADT::SparseBitvector;
#endif
    PointersT pointers;

    // if the pointer doesn't have ID, it's assigned one
    static size_t getPointerID(const Pointer &ptr) {
        return lookupTable.getOrCreate(ptr);
    }

    static const Pointer &getPointer(size_t id) { return lookupTable.get(id); }

    bool addWithUnknownOffset(PSNode *node) {
        auto ptrid = getPointerID({node, Offset::UNKNOWN});
        if (!pointers.get(ptrid)) {
            removeAny(node);
            return !pointers.set(ptrid);
        }
        return false; // we already had it
    }

  public:
    PointerIdPointsToSet() = default;
    explicit PointerIdPointsToSet(const std::initializer_list<Pointer> &elems) {
        add(elems);
    }

    bool add(PSNode *target, Offset off) { return add(Pointer(target, off)); }

    bool add(const Pointer &ptr) {
        if (has({ptr.target, Offset::UNKNOWN})) {
            return false;
        }
        if (ptr.offset.isUnknown()) {
            return addWithUnknownOffset(ptr.target);
        }
        return !pointers.set(getPointerID(ptr));
    }

    template <typename ContainerTy>
    bool add(const ContainerTy &C) {
        bool changed = false;
        for (const auto &ptr : C)
            changed |= add(ptr);
        return changed;
    }

    bool add(const PointerIdPointsToSet &S) { return pointers.set(S.pointers); }

    bool remove(const Pointer &ptr) {
        return pointers.unset(getPointerID(ptr));
    }

    bool remove(PSNode *target, Offset offset) {
        return remove(Pointer(target, offset));
    }

    bool removeAny(PSNode *target) {
        decltype(pointers) tmp;
        tmp.reserve(pointers.size());
        bool removed = false;
        for (const auto &ptrID : pointers) {
            if (lookupTable.get(ptrID).target != target) {
                tmp.set(ptrID);
            } else {
                removed = true;
            }
        }

        tmp.swap(pointers);

        return removed;
    }

    void clear() { pointers.reset(); }

    bool pointsTo(const Pointer &ptr) const {
        return pointers.get(getPointerID(ptr));
    }

    bool mayPointTo(const Pointer &ptr) const {
        return pointsTo(ptr) || pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
    }

    bool mustPointTo(const Pointer &ptr) const {
        assert(!ptr.offset.isUnknown() && "Makes no sense");
        return pointsTo(ptr) && isSingleton();
    }

    bool pointsToTarget(PSNode *target) const {
        for (auto ptrid : pointers) {
            const auto &ptr = getPointer(ptrid);
            if (ptr.target == target) {
                return true;
            }
        }
        return false;
    }

    bool isSingleton() const { return pointers.size() == 1; }

    bool empty() const { return pointers.empty(); }

    size_t count(const Pointer &ptr) const { return pointsTo(ptr); }

    bool has(const Pointer &ptr) const { return count(ptr) > 0; }

    bool hasUnknown() const { return pointsToTarget(UNKNOWN_MEMORY); }

    bool hasNull() const { return pointsToTarget(NULLPTR); }

    bool hasNullWithOffset() const {
        for (auto ptrid : pointers) {
            const auto &ptr = getPointer(ptrid);
            if (ptr.target == NULLPTR && *ptr.offset != 0) {
                return true;
            }
        }

        return false;
    }

    bool hasInvalidated() const { return pointsToTarget(INVALIDATED); }

    size_t size() const { return pointers.size(); }

    void swap(PointerIdPointsToSet &rhs) { pointers.swap(rhs.pointers); }

    class const_iterator {
        typename PointersT::const_iterator container_it;

        const_iterator(const PointersT &pointers, bool end = false)
                : container_it(end ? pointers.end() : pointers.begin()) {}

      public:
        const_iterator &operator++() {
            container_it++;
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        Pointer operator*() const { return {lookupTable.get(*container_it)}; }

        bool operator==(const const_iterator &rhs) const {
            return container_it == rhs.container_it;
        }

        bool operator!=(const const_iterator &rhs) const {
            return !operator==(rhs);
        }

        friend class PointerIdPointsToSet;
    };

    const_iterator begin() const { return {pointers}; }
    const_iterator end() const { return {pointers, true /* end */}; }

    friend class const_iterator;
};

} // namespace pta
} // namespace dg

#endif // DG_SINGLEBITVECTORPOINTSTOSET_H
