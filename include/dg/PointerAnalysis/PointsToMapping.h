#ifndef DG_POINTS_TO_MAPPING_H_
#define DG_POINTS_TO_MAPPING_H_

#include "PSNode.h"
#include <unordered_map>

namespace dg {
namespace pta {

// this is a wrapper around a map that
// is supposed to keep mapping of program values
// to pointer analysis nodes that are actually not
// created (or that are removed later by an analysis).
template <typename ValT>
class PointsToMapping {
    using MappingT = std::unordered_map<ValT, PSNode *>;
    using iterator = typename MappingT::iterator;
    using const_iterator = typename MappingT::const_iterator;

    MappingT mapping;

  public:
    void reserve(size_t s) { mapping.reserve(s); }

    size_t size() const { return mapping.size(); }

    PSNode *get(ValT val) const {
        auto it = mapping.find(val);
        if (it == mapping.end())
            return nullptr;

        return it->second;
    }

    void add(ValT val, PSNode *nd) {
        auto it = mapping.find(val);
        assert(it == mapping.end());
        mapping.emplace_hint(it, val, nd);
    }

    void set(ValT val, PSNode *nd) { mapping[val] = nd; }

    // merge some other points-to mapping to this one
    // (destroying the other one). If there are
    // duplicates values, than the ones from 'rhs'
    // are used
    void merge(PointsToMapping &&rhs) {
        // merge values from this map to rhs (making preference for
        // duplicate rhs values).
        rhs.mapping.insert(mapping.begin(), mapping.end());
        rhs.mapping.swap(mapping);
        rhs.mapping.clear();
    }

    // compose this mapping with some other mapping:
    // (PSNode * -> PSNode *) o (ValT -> PSNode *)
    // leads to (ValT -> PSNode *).
    void compose(PointsToMapping<PSNode *> &&rhs) {
        for (auto &it : mapping) {
            if (PSNode *rhs_node = rhs.get(it.second)) {
                it.second = rhs_node;
            }
        }
    }

    iterator begin() { return mapping.begin(); }
    iterator end() { return mapping.end(); }
    const_iterator begin() const { return mapping.begin(); }
    const_iterator end() const { return mapping.end(); }
};

} // namespace pta
} // namespace dg

#endif
