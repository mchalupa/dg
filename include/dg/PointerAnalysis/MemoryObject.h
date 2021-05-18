#ifndef DG_MEMORY_OBJECT_H_
#define DG_MEMORY_OBJECT_H_

#include <cassert>
#include <map>
#include <set>
#include <unordered_map>

#ifndef NDEBUG
#include "dg/PointerAnalysis/PSNode.h"
#include <iostream>
#endif // not NDEBUG

#include "PointsToSet.h"

namespace dg {
namespace pta {

struct MemoryObject {
    using PointsToMapT = std::map<Offset, PointsToSetT>;

    MemoryObject(/*uint64_t s = 0, bool isheap = false, */ PSNode *n = nullptr)
            : node(n) /*, is_heap(isheap), size(s)*/ {}

    // where was this memory allocated? for debugging
    PSNode *node;
    // possible pointers stored in this memory object
    PointsToMapT pointsTo;

    PointsToSetT &getPointsTo(const Offset off) { return pointsTo[off]; }

    PointsToMapT::iterator find(const Offset off) { return pointsTo.find(off); }

    PointsToMapT::const_iterator find(const Offset off) const {
        return pointsTo.find(off);
    }

    PointsToMapT::iterator begin() { return pointsTo.begin(); }
    PointsToMapT::iterator end() { return pointsTo.end(); }
    PointsToMapT::const_iterator begin() const { return pointsTo.begin(); }
    PointsToMapT::const_iterator end() const { return pointsTo.end(); }

    bool merge(const MemoryObject &rhs) {
        bool changed = false;
        for (const auto &rit : rhs.pointsTo) {
            if (rit.second.empty())
                continue;
            changed |= pointsTo[rit.first].add(rit.second);
        }

        return changed;
    }

    bool addPointsTo(const Offset &off, const Pointer &ptr) {
        assert(ptr.target != nullptr &&
               "Cannot have NULL target, use unknown instead");

        return pointsTo[off].add(ptr);
    }

    bool addPointsTo(const Offset &off, const PointsToSetT &pointers) {
        if (pointers.empty())
            return false;
        return pointsTo[off].add(pointers);
    }

    bool addPointsTo(const Offset &off,
                     std::initializer_list<Pointer> pointers) {
        if (pointers.size() == 0)
            return false;
        return pointsTo[off].add(pointers);
    }

#ifndef NDEBUG
    void dump() const {
        std::cout << "MO [" << this << "] for ";
        node->dump();
    }

    void dumpv() const {
        dump();
        for (const auto &it : pointsTo) {
            std::cout << "[";
            it.first.dump();
            std::cout << "]";
            for (const auto &ptr : it.second) {
                std::cout << "  -> ";
                ptr.dump();
                std::cout << "\n";
            }
        }
        std::cout << "\n";
    }

    void print() const {
        dump();
        std::cout << "\n";
    }
#endif // not NDEBUG
};

} // namespace pta
} // namespace dg

#endif // DG_MEMORY_OBJECT_H_
