#ifndef DG_OFFSETS_SET_PTSET_H
#define DG_OFFSETS_SET_PTSET_H

#include "dg/ADT/Bitvector.h"
#include "dg/PointerAnalysis/Pointer.h"

#include <cassert>
#include <map>

namespace dg {
namespace pta {

// declare PSNode
class PSNode;

class OffsetsSetPointsToSet {
    // each pointer is a pair (PSNode *, {offsets}),
    // so we represent them coinciesly this way
    using ContainerT = std::map<PSNode *, ADT::SparseBitvector>;
    ContainerT pointers;

    bool addWithUnknownOffset(PSNode *target) {
        auto it = pointers.find(target);
        if (it != pointers.end()) {
            // we already had that offset?
            if (it->second.get(Offset::UNKNOWN))
                return false;

            // get rid of other offsets and keep
            // only the unknown offset
            it->second.reset();
            it->second.set(Offset::UNKNOWN);
            return true;
        }

        return !pointers[target].set(Offset::UNKNOWN);
    }

  public:
    OffsetsSetPointsToSet() = default;
    OffsetsSetPointsToSet(std::initializer_list<Pointer> elems) { add(elems); }

    bool add(PSNode *target, Offset off) {
        if (off.isUnknown())
            return addWithUnknownOffset(target);

        auto it = pointers.find(target);
        if (it == pointers.end()) {
            pointers.emplace_hint(it, target, *off);
            return true;
        }
        if (it->second.get(Offset::UNKNOWN))
            return false;
        // the set will return the previous value
        // of the bit, so that means false if we are
        // setting a new value
        return !it->second.set(*off);
    }

    bool add(const Pointer &ptr) { return add(ptr.target, ptr.offset); }

    // union (unite S into this set)
    bool add(const OffsetsSetPointsToSet &S) {
        bool changed = false;
        for (const auto &it : S.pointers) {
            changed |= pointers[it.first].set(it.second);
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

    bool remove(const Pointer &ptr) { return remove(ptr.target, ptr.offset); }

    ///
    // Remove pointer to this target with this offset.
    // This is method really removes the pair
    // (target, off) even when the off is unknown
    bool remove(PSNode *target, Offset offset) {
        auto it = pointers.find(target);
        if (it == pointers.end()) {
            return false;
        }

        bool ret = it->second.unset(*offset);
        if (ret && it->second.empty()) {
            pointers.erase(it);
        }
        assert((ret || !it->second.empty()) && "Inconsistence");
        return ret;
    }

    ///
    // Remove pointers pointing to this target
    bool removeAny(PSNode *target) {
        auto it = pointers.find(target);
        if (it == pointers.end()) {
            return false;
        }

        pointers.erase(it);
        return true;
    }

    void clear() { pointers.clear(); }

    bool pointsTo(const Pointer &ptr) const {
        auto it = pointers.find(ptr.target);
        if (it == pointers.end())
            return false;
        return it->second.get(*ptr.offset);
    }

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
        return pointers.find(target) != pointers.end();
    }

    bool isSingleton() const { return pointers.size() == 1; }

    bool empty() const { return pointers.empty(); }

    size_t count(const Pointer &ptr) const {
        auto it = pointers.find(ptr.target);
        if (it != pointers.end()) {
            return it->second.get(*ptr.offset);
        }

        return 0;
    }

    bool has(const Pointer &ptr) const { return count(ptr) > 0; }

    bool hasUnknown() const { return pointsToTarget(UNKNOWN_MEMORY); }
    bool hasNull() const { return pointsToTarget(NULLPTR); }
    bool hasInvalidated() const { return pointsToTarget(INVALIDATED); }

    size_t size() const {
        size_t num = 0;
        for (const auto &it : pointers) {
            num += it.second.size();
        }

        return num;
    }

    void swap(OffsetsSetPointsToSet &rhs) { pointers.swap(rhs.pointers); }

    class const_iterator {
        typename ContainerT::const_iterator container_it;
        typename ContainerT::const_iterator container_end;
        typename ADT::SparseBitvector::const_iterator innerIt;

        const_iterator(const ContainerT &cont, bool end = false)
                : container_it(end ? cont.end() : cont.begin()),
                  container_end(cont.end()) {
            if (container_it != container_end) {
                innerIt = container_it->second.begin();
            }
        }

      public:
        const_iterator &operator++() {
            ++innerIt;
            if (innerIt == container_it->second.end()) {
                ++container_it;
                if (container_it != container_end)
                    innerIt = container_it->second.begin();
                else
                    innerIt = ADT::SparseBitvector::const_iterator();
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        Pointer operator*() const { return {container_it->first, *innerIt}; }

        bool operator==(const const_iterator &rhs) const {
            return container_it == rhs.container_it && innerIt == rhs.innerIt;
        }

        bool operator!=(const const_iterator &rhs) const {
            return !operator==(rhs);
        }

        friend class OffsetsSetPointsToSet;
    };

    const_iterator begin() const { return {pointers}; }
    const_iterator end() const { return {pointers, true /* end */}; }

    friend class const_iterator;
};

} // namespace pta
} // namespace dg

#endif // DG_OFFSETS_SET_PTSET_H
