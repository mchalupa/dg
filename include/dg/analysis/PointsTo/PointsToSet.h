#ifndef _DG_POINTS_TO_SET_H_
#define _DG_POINTS_TO_SET_H_

#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/ADT/Bitvector.h"

#include <map>
#include <set>
#include <cassert>

namespace dg {
namespace analysis {
namespace pta {

// declare PSNode
class PSNode;

class PointsToSet {
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
    PointsToSet() = default;
    PointsToSet(std::initializer_list<Pointer> elems) { add(elems); }

    bool add(PSNode *target, Offset off) {
        if (off.isUnknown())
            return addWithUnknownOffset(target);

        auto it = pointers.find(target);
        if (it == pointers.end()) {
            pointers.emplace_hint(it, target, *off);
            return true;
        } else {
            if (it->second.get(Offset::UNKNOWN))
                return false;
            else {
                // the set will return the previous value
                // of the bit, so that means false if we are
                // setting a new value
                return !it->second.set(*off);
            }
        }
    }

    bool add(const Pointer& ptr) {
        return add(ptr.target, ptr.offset);
    }

    // union (unite S into this set)
    bool add(const PointsToSet& S) {
        bool changed = false;
        for (auto& it : S.pointers) {
            changed |= pointers[it.first].set(it.second);
        }
        return changed;
    }

    bool add(std::initializer_list<Pointer> elems) {
        bool changed = false;
        for (const auto& e : elems) {
            changed |= add(e);
        }
        return changed;
    }

    bool remove(const Pointer& ptr) {
        return remove(ptr.target, ptr.offset);
    }

    ///
    // Remove pointer to this target with this offset.
    // This is method really removes the pair
    // (target, off) even when the off is unknown
    bool remove(PSNode *target, Offset offset) {
        auto it = pointers.find(target);
        if (it == pointers.end()) {
            return false;
        }

        return it->second.unset(*offset);
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

    bool pointsTo(const Pointer& ptr) const {
        auto it = pointers.find(ptr.target);
        if (it == pointers.end())
            return false;
        return it->second.get(*ptr.offset);
    }

    // points to the pointer or the the same target
    // with unknown offset? Note: we do not count
    // unknown memory here...
    bool mayPointTo(const Pointer& ptr) const {
        return pointsTo(ptr) ||
                pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
    }

    bool mustPointTo(const Pointer& ptr) const {
        assert(!ptr.offset.isUnknown() && "Makes no sense");
        return pointsTo(ptr) && isSingleton();
    }

    bool pointsToTarget(PSNode *target) const {
        return pointers.find(target) != pointers.end();
    }

    bool isSingleton() const {
        return pointers.size() == 1;
    }

    bool empty() const { return pointers.empty(); }

    size_t count(const Pointer& ptr) const {
        auto it = pointers.find(ptr.target);
        if (it != pointers.end()) {
            return it->second.get(*ptr.offset);
        }

        return 0;
    }

    bool has(const Pointer& ptr) const {
        return count(ptr) > 0;
    }

    bool hasUnknown() const { return pointsToTarget(UNKNOWN_MEMORY); }
    bool hasNull() const { return pointsToTarget(NULLPTR); }
    bool hasInvalidated() const { return pointsToTarget(INVALIDATED); }

    size_t size() const {
        size_t num = 0;
        for (auto& it : pointers) {
            num += it.second.size();
        }

        return num;
    }

    void swap(PointsToSet& rhs) { pointers.swap(rhs.pointers); }

    class const_iterator {
        typename ContainerT::const_iterator container_it;
        typename ContainerT::const_iterator container_end;
        typename ADT::SparseBitvector::const_iterator innerIt;

        const_iterator(const ContainerT& cont, bool end = false)
        : container_it(end ? cont.end() : cont.begin()), container_end(cont.end()) {
            if (container_it != container_end) {
                innerIt = container_it->second.begin();
            }
        }
    public:
        const_iterator& operator++() {
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

        Pointer operator*() const {
            return Pointer(container_it->first, *innerIt);
        }

        bool operator==(const const_iterator& rhs) const {
            return container_it == rhs.container_it && innerIt == rhs.innerIt;
        }

        bool operator!=(const const_iterator& rhs) const {
            return !operator==(rhs);
        }

        friend class PointsToSet;
    };

    const_iterator begin() const { return const_iterator(pointers); }
    const_iterator end() const { return const_iterator(pointers, true /* end */); }

    friend class const_iterator;
};


///
// We keep the implementation of this points-to set because
// it is good for comparison and regression testing
class SimplePointsToSet {
    using ContainerT = std::set<Pointer>;
    ContainerT pointers;

    using const_iterator = typename ContainerT::const_iterator;

    bool addWithUnknownOffset(PSNode *target) {
        if (has({target, Offset::UNKNOWN}))
            return false;

        ContainerT tmp;
        for (const auto& ptr : pointers) {
            if (ptr.target != target)
                tmp.insert(ptr);
        }

        tmp.swap(pointers);
        return pointers.insert({target, Offset::UNKNOWN}).second;
    }

public:
    SimplePointsToSet() = default;
    SimplePointsToSet(std::initializer_list<Pointer> elems) { add(elems); }

    bool add(PSNode *target, Offset off) {
        if (off.isUnknown())
            return addWithUnknownOffset(target);

        // if we have the same pointer but with unknown offset,
        // do nothing
        if (has({target, Offset::UNKNOWN}))
            return false;

        return pointers.emplace(target, off).second;
    }

    bool add(const Pointer& ptr) {
        return add(ptr.target, ptr.offset);
    }

    // make union of the two sets and store it
    // into 'this' set (i.e. merge rhs to this set)
    bool add(const SimplePointsToSet& rhs) {
        bool changed = false;
        for (const auto& ptr : rhs.pointers) {
            changed |= pointers.insert(ptr).second;
        }

        return changed;
    }

    bool add(std::initializer_list<Pointer> elems) {
        bool changed = false;
        for (const auto& e : elems) {
            changed |= add(e);
        }
        return changed;
    }

    bool remove(const Pointer& ptr) {
        return pointers.erase(ptr) != 0;
    }

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
            for (const auto& ptr : pointers) {
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

    bool pointsTo(const Pointer& ptr) const {
        return pointers.count(ptr) > 0;
    }

    // points to the pointer or the the same target
    // with unknown offset? Note: we do not count
    // unknown memory here...
    bool mayPointTo(const Pointer& ptr) const {
        return pointsTo(ptr) ||
                pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
    }

    bool mustPointTo(const Pointer& ptr) const {
        assert(!ptr.offset.isUnknown() && "Makes no sense");
        return pointsTo(ptr) && isSingleton();
    }

    bool pointsToTarget(PSNode *target) const {
        for (const auto& ptr : pointers) {
            if (ptr.target == target)
                return true;
        }
        return false;
    }

    bool isSingleton() const {
        return pointers.size() == 1;
    }

    size_t count(const Pointer& ptr) { return pointers.count(ptr); }
    size_t size() { return pointers.size(); }
    bool empty() const { return pointers.empty(); }
    bool has(const Pointer& ptr) { return count(ptr) > 0; }
    bool hasUnknown() const { return pointsToTarget(UNKNOWN_MEMORY); }
    bool hasNull() const { return pointsToTarget(NULLPTR); }
    bool hasInvalidated() const { return pointsToTarget(INVALIDATED); }

    void swap(SimplePointsToSet& rhs) { pointers.swap(rhs.pointers); }

    const_iterator begin() const { return pointers.begin(); }
    const_iterator end() const { return pointers.end(); }
};



using PointsToSetT = PointsToSet;
using PointsToMapT = std::map<Offset, PointsToSetT>;

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_POINTS_TO_SET_H_
