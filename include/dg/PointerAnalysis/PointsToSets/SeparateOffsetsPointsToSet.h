#ifndef SEPARATEOFFSETSPOINTSTOSET_H
#define SEPARATEOFFSETSPOINTSTOSET_H

#include "dg/ADT/Bitvector.h"
#include "dg/PointerAnalysis/Pointer.h"

#include <cstdlib>
#include <map>
#include <vector>

namespace dg {
namespace pta {

class PSNode;

class SeparateOffsetsPointsToSet {
    ADT::SparseBitvector nodes;
    ADT::SparseBitvector offsets;
    static std::map<PSNode *, size_t> ids; // nodes are numbered 1, 2, ...
    static std::vector<PSNode *>
            idVector; // starts from 0 (node = idVector[id - 1])

    // if the node doesn't have ID, it is assigned one
    static size_t getNodeID(PSNode *node) {
        auto it = ids.find(node);
        if (it != ids.end()) {
            return it->second;
        }
        idVector.push_back(node);
        return ids.emplace_hint(it, node, ids.size() + 1)->second;
    }

  public:
    SeparateOffsetsPointsToSet() = default;
    SeparateOffsetsPointsToSet(std::initializer_list<Pointer> elems) {
        add(elems);
    }

    bool add(PSNode *target, Offset off) {
        if (offsets.get(Offset::UNKNOWN)) {
            return !nodes.set(getNodeID(target));
        }
        if (off.isUnknown()) {
            offsets.reset();
        }
        bool changed = !nodes.set(getNodeID(target));
        return !offsets.set(*off) || changed;
    }

    bool add(const Pointer &ptr) { return add(ptr.target, ptr.offset); }

    bool add(const SeparateOffsetsPointsToSet &S) {
        bool changed = nodes.set(S.nodes);
        return offsets.set(S.offsets) || changed;
    }

    static bool remove(__attribute__((unused)) const Pointer &ptr) { abort(); }

    static bool remove(__attribute__((unused)) PSNode *target,
                       __attribute__((unused)) Offset offset) {
        abort();
    }

    static bool removeAny(__attribute__((unused)) PSNode *target) { abort(); }

    void clear() {
        nodes.reset();
        offsets.reset();
    }

    bool pointsTo(const Pointer &ptr) const {
        return nodes.get(getNodeID(ptr.target)) && offsets.get(*ptr.offset);
    }

    bool mayPointTo(const Pointer &ptr) const {
        return pointsTo(ptr) || pointsTo(Pointer(ptr.target, Offset::UNKNOWN));
    }

    bool mustPointTo(const Pointer &ptr) const {
        return (nodes.size() == 1 || offsets.size() == 1) && pointsTo(ptr);
    }

    bool pointsToTarget(PSNode *target) const {
        return nodes.get(getNodeID(target));
    }

    bool isSingleton() const {
        return nodes.size() == 1 && offsets.size() == 1;
    }

    bool empty() const { return nodes.empty() && offsets.empty(); }

    size_t count(const Pointer &ptr) const { return pointsTo(ptr); }

    bool has(const Pointer &ptr) const { return count(ptr) > 0; }

    bool hasUnknown() const { return pointsToTarget(UNKNOWN_MEMORY); }

    bool hasNull() const { return pointsToTarget(NULLPTR); }

    bool hasInvalidated() const { return pointsToTarget(INVALIDATED); }

    size_t size() const { return nodes.size() * offsets.size(); }

    void swap(SeparateOffsetsPointsToSet &rhs) {
        nodes.swap(rhs.nodes);
        offsets.swap(rhs.offsets);
    }

    // iterates through all the possible combinations of nodes and their offsets
    // stored in this points-to set
    class const_iterator {
        typename ADT::SparseBitvector::const_iterator nodes_it;
        typename ADT::SparseBitvector::const_iterator nodes_end;
        typename ADT::SparseBitvector::const_iterator offsets_it;
        typename ADT::SparseBitvector::const_iterator offsets_begin;
        typename ADT::SparseBitvector::const_iterator offsets_end;

        const_iterator(const ADT::SparseBitvector &nodes,
                       const ADT::SparseBitvector &offsets, bool end = false)
                : nodes_it(end ? nodes.end() : nodes.begin()),
                  nodes_end(nodes.end()), offsets_it(offsets.begin()),
                  offsets_begin(offsets.begin()), offsets_end(offsets.end()) {
            if (nodes_it == nodes_end) {
                offsets_it = offsets_end;
            }
        }

      public:
        const_iterator &operator++() {
            offsets_it++;
            if (offsets_it == offsets_end) {
                nodes_it++;
                if (nodes_it != nodes_end) {
                    offsets_it = offsets_begin;
                }
            }
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            operator++();
            return tmp;
        }

        Pointer operator*() const {
            return {idVector[*nodes_it - 1], *offsets_it};
        }

        bool operator==(const const_iterator &rhs) const {
            return nodes_it == rhs.nodes_it && offsets_it == rhs.offsets_it;
        }

        bool operator!=(const const_iterator &rhs) const {
            return !operator==(rhs);
        }

        friend class SeparateOffsetsPointsToSet;
    };

    const_iterator begin() const { return {nodes, offsets}; }
    const_iterator end() const { return {nodes, offsets, true /* end */}; }

    friend class const_iterator;
};

} // namespace pta
} // namespace dg

#endif /* SEPARATEOFFSETSPOINTSTOSET_H */
