#ifndef ALIGNEDOFFSETSPOINTSTOSET_H
#define ALIGNEDOFFSETSPOINTSTOSET_H

#include "dg/ADT/Bitvector.h"
#include "dg/PointerAnalysis/Pointer.h"

#include <cassert>
#include <map>
#include <set>
#include <vector>

namespace dg {
namespace pta {

class PSNode;

class AlignedSmallOffsetsPointsToSet {
    static const size_t MAX_OFFSET = 63;
    static const unsigned int multiplier =
            4; // offsets that are divisible by this value are stored in
               // bitvector up to 62 * multiplier

    ADT::SparseBitvector pointers;
    std::set<Pointer> oddPointers;
    static std::map<PSNode *, size_t> ids; // nodes are numbered 1,2, ...
    static std::vector<PSNode *>
            idVector; // starts from 0 (node = idVector[id - 1])

    // if the node doesn't have ID, it's assigned one
    static size_t getNodeID(PSNode *node) {
        auto it = ids.find(node);
        if (it != ids.end()) {
            return it->second;
        }
        idVector.push_back(node);
        return ids.emplace_hint(it, node, ids.size() + 1)->second;
    }

    static size_t getNodePosition(PSNode *node) {
        return ((getNodeID(node) - 1) * (MAX_OFFSET + 1));
    }

    static size_t getPosition(PSNode *node, Offset off) {
        if (off.isUnknown()) {
            return getNodePosition(node) + MAX_OFFSET;
        }
        return getNodePosition(node) + (*off / multiplier);
    }

    static bool isOffsetValid(Offset off) {
        return off.isUnknown() || (*off <= (MAX_OFFSET - 1) * multiplier &&
                                   *off % multiplier == 0);
    }

    bool addWithUnknownOffset(PSNode *target) {
        removeAny(target);
        return !pointers.set(getPosition(target, Offset::UNKNOWN));
    }

  public:
    AlignedSmallOffsetsPointsToSet() = default;
    AlignedSmallOffsetsPointsToSet(std::initializer_list<Pointer> elems) {
        add(elems);
    }

    bool add(PSNode *target, Offset off) {
        if (has({target, Offset::UNKNOWN})) {
            return false;
        }
        if (off.isUnknown()) {
            return addWithUnknownOffset(target);
        }
        if (isOffsetValid(off)) {
            return !pointers.set(getPosition(target, off));
        }
        return oddPointers.emplace(target, off).second;
    }

    bool add(const Pointer &ptr) { return add(ptr.target, ptr.offset); }

    bool add(const AlignedSmallOffsetsPointsToSet &S) {
        bool changed = pointers.set(S.pointers);
        for (const auto &ptr : S.oddPointers) {
            changed |= oddPointers.insert(ptr).second;
        }
        return changed;
    }

    bool remove(const Pointer &ptr) {
        if (isOffsetValid(ptr.offset)) {
            return pointers.unset(getPosition(ptr.target, ptr.offset));
        }
        return oddPointers.erase(ptr) != 0;
    }

    bool remove(PSNode *target, Offset offset) {
        return remove(Pointer(target, offset));
    }

    bool removeAny(PSNode *target) {
        bool changed = false;
        size_t position = getNodePosition(target);
        for (size_t i = position; i < position + (MAX_OFFSET + 1); i++) {
            changed |= pointers.unset(i);
        }
        auto it = oddPointers.begin();
        while (it != oddPointers.end()) {
            if (it->target == target) {
                it = oddPointers.erase(it);
                changed = true;
            } else {
                it++;
            }
        }
        return changed;
    }

    void clear() {
        pointers.reset();
        oddPointers.clear();
    }

    bool pointsTo(const Pointer &ptr) const {
        if (isOffsetValid(ptr.offset)) {
            return pointers.get(getPosition(ptr.target, ptr.offset));
        }
        return oddPointers.find(ptr) != oddPointers.end();
    }

    bool mayPointTo(const Pointer &ptr) const {
        return pointsTo(ptr) || pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
    }

    bool mustPointTo(const Pointer &ptr) const {
        assert(!ptr.offset.isUnknown() && "Makes no sense");
        return pointsTo(ptr) && isSingleton();
    }

    bool pointsToTarget(PSNode *target) const {
        size_t position = getNodePosition(target);
        for (size_t i = position; i < position + (MAX_OFFSET + 1); i++) {
            if (pointers.get(i))
                return true;
        }
        return dg::any_of(oddPointers, [target](const Pointer &ptr) {
            return ptr.target == target;
        });
    }

    bool isSingleton() const {
        return (pointers.size() == 1 && oddPointers.empty()) ||
               (pointers.empty() && oddPointers.size() == 1);
    }

    bool empty() const { return pointers.empty() && oddPointers.empty(); }

    size_t count(const Pointer &ptr) const { return pointsTo(ptr); }

    bool has(const Pointer &ptr) const { return count(ptr) > 0; }

    bool hasUnknown() const { return pointsToTarget(UNKNOWN_MEMORY); }

    bool hasNull() const { return pointsToTarget(NULLPTR); }

    bool hasInvalidated() const { return pointsToTarget(INVALIDATED); }

    size_t size() const { return pointers.size() + oddPointers.size(); }

    void swap(AlignedSmallOffsetsPointsToSet &rhs) {
        pointers.swap(rhs.pointers);
        oddPointers.swap(rhs.oddPointers);
    }

    size_t overflowSetSize() const { return oddPointers.size(); }

    static unsigned int getMultiplier() { return multiplier; }

    // iterates over the bitvector first, then over the set
    class const_iterator {
        typename ADT::SparseBitvector::const_iterator bitvector_it;
        typename ADT::SparseBitvector::const_iterator bitvector_end;
        typename std::set<Pointer>::const_iterator set_it;
        bool secondContainer;

        const_iterator(const ADT::SparseBitvector &pointers,
                       const std::set<Pointer> &oddPointers, bool end = false)
                : bitvector_it(end ? pointers.end() : pointers.begin()),
                  bitvector_end(pointers.end()),
                  set_it(end ? oddPointers.end() : oddPointers.begin()),
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
                size_t offsetPosition = (*bitvector_it % (MAX_OFFSET + 1));
                size_t nodeID =
                        ((*bitvector_it - offsetPosition) / (MAX_OFFSET + 1)) +
                        1;
                return offsetPosition == MAX_OFFSET
                               ? Pointer(idVector[nodeID - 1], Offset::UNKNOWN)
                               : Pointer(idVector[nodeID - 1],
                                         offsetPosition * multiplier);
            }
            return *set_it;
        }

        bool operator==(const const_iterator &rhs) const {
            return bitvector_it == rhs.bitvector_it && set_it == rhs.set_it;
        }

        bool operator!=(const const_iterator &rhs) const {
            return !operator==(rhs);
        }

        friend class AlignedSmallOffsetsPointsToSet;
    };

    const_iterator begin() const { return {pointers, oddPointers}; }
    const_iterator end() const {
        return {pointers, oddPointers, true /* end */};
    }

    friend class const_iterator;
};

} // namespace pta
} // namespace dg

#endif /* ALIGNEDOFFSETSPOINTSTOSET_H */
