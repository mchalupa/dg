#ifndef DG_POINTS_TO_MAPPING_H_
#define DG_POINTS_TO_MAPPING_H_

#include <unordered_map>

namespace dg {
namespace pta {

class PSNode;

// this is a wrapper around a map that
// is supposed to keep mapping of program values
// to pointer analysis nodes that are actually not
// created (or that are removed later by an analysis).
template <typename KeyT, typename ValT = PSNode*>
class PointsToMapping {
    using MappingT = std::unordered_map<KeyT, ValT>;
    using iterator = typename MappingT::iterator;
    using const_iterator = typename MappingT::const_iterator;

    MappingT mapping;
public:
    void reserve(size_t s) {
        mapping.reserve(s);
    }

    size_t size() const { return mapping.size(); }

    bool has(KeyT val) const {
        auto it = mapping.find(val);
        return it != mapping.end();
    }

    ValT get(KeyT val) const {
        auto it = mapping.find(val);
        if (it == mapping.end())
            return ValT{0};

        return it->second;
    }

    void add(KeyT val, ValT nd) {
        auto it = mapping.find(val);
        assert(it == mapping.end());
        mapping.emplace_hint(it, val, nd);
    }

    void set(KeyT val, ValT nd) {
        mapping[val] = nd;
    }

    // merge some other points-to mapping to this one
    // (destroying the other one). If there are
    // duplicates values, than the ones from 'rhs'
    // are used
    void merge(PointsToMapping&& rhs) {
        // merge values from this map to rhs (making preference for
        // duplicate rhs values).
        rhs.mapping.insert(mapping.begin(), mapping.end());
        rhs.mapping.swap(mapping);
        rhs.mapping.clear();
    }

    // compose this mapping with some other mapping:
    // (ValT* -> ValT) o (KeyT -> ValT)
    // leads to (KeyT -> ValT).
    void compose(PointsToMapping&& rhs) {
        for (auto& it : mapping) {
            if (auto rhs_node = rhs.get(it.second)) {
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
