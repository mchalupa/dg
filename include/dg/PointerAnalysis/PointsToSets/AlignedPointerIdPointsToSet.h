#ifndef DG_ALIGNEDBITVECTORPOINTSTOSET_H
#define DG_ALIGNEDBITVECTORPOINTSTOSET_H

#include "dg/ADT/Bitvector.h"
#include "dg/PointerAnalysis/Pointer.h"
#include "dg/util/iterators.h"

#include <cassert>
#include <map>
#include <set>
#include <vector>

namespace dg {
namespace pta {

class PSNode;

class AlignedPointerIdPointsToSet {
    static const unsigned int multiplier = 4;

    ADT::SparseBitvector pointers;
    std::set<Pointer> overflowSet;
    static std::map<Pointer, size_t> ids; // pointers are numbered 1, 2, ...
    static std::vector<Pointer>
            idVector; // starts from 0 (pointer = idVector[id - 1])

    // if the pointer doesn't have ID, it's assigned one
    static size_t getPointerID(const Pointer &ptr) {
        auto it = ids.find(ptr);
        if (it != ids.end()) {
            return it->second;
        }
        idVector.push_back(ptr);
        return ids.emplace_hint(it, ptr, ids.size() + 1)->second;
    }

    bool addWithUnknownOffset(PSNode *node) {
        removeAny(node);
        return !pointers.set(getPointerID({node, Offset::UNKNOWN}));
    }

    static bool isOffsetValid(Offset off) {
        return off.isUnknown() || *off % multiplier == 0;
    }

  public:
    AlignedPointerIdPointsToSet() = default;
    AlignedPointerIdPointsToSet(std::initializer_list<Pointer> elems) {
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
        if (isOffsetValid(ptr.offset)) {
            return !pointers.set(getPointerID(ptr));
        }
        return overflowSet.insert(ptr).second;
    }

    bool add(const AlignedPointerIdPointsToSet &S) {
        bool changed = pointers.set(S.pointers);
        for (const auto &ptr : S.overflowSet) {
            changed |= overflowSet.insert(ptr).second;
        }
        return changed;
    }

    bool remove(const Pointer &ptr) {
        if (isOffsetValid(ptr.offset)) {
            return pointers.unset(getPointerID(ptr));
        }
        return overflowSet.erase(ptr) != 0;
    }

    bool remove(PSNode *target, Offset offset) {
        return remove(Pointer(target, offset));
    }

    bool removeAny(PSNode *target) {
        std::vector<size_t> toRemove;
        for (const auto &ptrID : pointers) {
            if (idVector[ptrID - 1].target == target) {
                toRemove.push_back(ptrID);
            }
        }

        for (auto ptrID : toRemove) {
            pointers.unset(ptrID);
        }

        bool changed = false;
        auto it = overflowSet.begin();
        while (it != overflowSet.end()) {
            if (it->target == target) {
                it = overflowSet.erase(it);
                // Note: the iterator to the next element is now in it
                changed = true;
            } else {
                it++;
            }
        }
        return changed || !toRemove.empty();
    }

    void clear() {
        pointers.reset();
        overflowSet.clear();
    }

    bool pointsTo(const Pointer &ptr) const {
        if (isOffsetValid(ptr.offset)) {
            return pointers.get(getPointerID(ptr));
        }
        return overflowSet.find(ptr) != overflowSet.end();
    }

    bool mayPointTo(const Pointer &ptr) const {
        return pointsTo(ptr) || pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
    }

    bool mustPointTo(const Pointer &ptr) const {
        assert(!ptr.offset.isUnknown() && "Makes no sense");
        return pointsTo(ptr) && isSingleton();
    }

    bool pointsToTarget(PSNode *target) const {
        for (const auto &kv : ids) {
            if (kv.first.target == target && pointers.get(kv.second)) {
                return true;
            }
        }
        return dg::any_of(overflowSet, [target](const Pointer &ptr) {
            return ptr.target == target;
        });
    }

    bool isSingleton() const {
        return (pointers.size() == 1 && overflowSet.empty()) ||
               (pointers.empty() && overflowSet.size() == 1);
    }

    bool empty() const { return pointers.empty() && overflowSet.empty(); }

    size_t count(const Pointer &ptr) const { return pointsTo(ptr); }

    bool has(const Pointer &ptr) const { return count(ptr) > 0; }

    bool hasUnknown() const { return pointsToTarget(UNKNOWN_MEMORY); }

    bool hasNull() const { return pointsToTarget(NULLPTR); }

    bool hasInvalidated() const { return pointsToTarget(INVALIDATED); }

    size_t size() const { return pointers.size() + overflowSet.size(); }

    void swap(AlignedPointerIdPointsToSet &rhs) {
        pointers.swap(rhs.pointers);
        overflowSet.swap(rhs.overflowSet);
    }

    size_t overflowSetSize() const { return overflowSet.size(); }

    static unsigned int getMultiplier() { return multiplier; }

    class const_iterator {
        typename ADT::SparseBitvector::const_iterator bitvector_it;
        typename ADT::SparseBitvector::const_iterator bitvector_end;
        typename std::set<Pointer>::const_iterator set_it;
        bool secondContainer;

        const_iterator(const ADT::SparseBitvector &pointers,
                       const std::set<Pointer> &overflow, bool end = false)
                : bitvector_it(end ? pointers.end() : pointers.begin()),
                  bitvector_end(pointers.end()),
                  set_it(end ? overflow.end() : overflow.begin()),
                  secondContainer(end) {
            if (bitvector_it == bitvector_end) {
                secondContainer = true;
            }
        }

      public:
        const_iterator &operator++() {
            if (!secondContainer) {
                bitvector_it++;
                if (bitvector_it == bitvector_end) {
                    secondContainer = true;
                }
            } else {
                set_it++;
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        Pointer operator*() const {
            if (!secondContainer) {
                return {idVector[*bitvector_it - 1]};
            }
            return *set_it;
        }

        bool operator==(const const_iterator &rhs) const {
            return bitvector_it == rhs.bitvector_it && set_it == rhs.set_it;
        }

        bool operator!=(const const_iterator &rhs) const {
            return !operator==(rhs);
        }

        friend class AlignedPointerIdPointsToSet;
    };

    const_iterator begin() const { return {pointers, overflowSet}; }
    const_iterator end() const {
        return {pointers, overflowSet, true /* end */};
    }

    friend class const_iterator;
};

} // namespace pta
} // namespace dg

#endif // DG_ALIGNEDBITVECTORPOINTSTOSET_H
