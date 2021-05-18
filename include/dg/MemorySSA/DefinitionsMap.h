#ifndef DG_DEFINITIONS_MAP_H_
#define DG_DEFINITIONS_MAP_H_

#include <set>
#include <unordered_map>
#include <vector>
#ifndef NDEBUG
#include <iostream>
#endif

#include "dg/ADT/DisjunctiveIntervalMap.h"
#include "dg/Offset.h"
#include "dg/ReadWriteGraph/DefSite.h"

namespace dg {
namespace dda {

class RWNode;
class ReachingDefinitionsAnalysis;

/// A data structure that represents a mapping
/// DefSite -> RWNode, that is, it stores which memory (DefSite)
/// was defined where.
template <typename NodeT = RWNode>
class DefinitionsMap {
  public:
    using OffsetsT = ADT::DisjunctiveIntervalMap<NodeT *>;
    using IntervalT = typename OffsetsT::IntervalT;

  private:
    std::unordered_map<NodeT *, OffsetsT> _definitions{};

    // transform (offset, lenght) from a DefSite into the interval
    static std::pair<Offset, Offset> getInterval(const DefSite &ds) {
        // if the offset is unknown, stretch the interval over all possible
        // bytes
        if (ds.offset.isUnknown())
            return {0, Offset::UNKNOWN};

        return {ds.offset, ds.offset + (ds.len - 1)};
    }

  public:
    void clear() { _definitions.clear(); }
    void swap(DefinitionsMap &rhs) { _definitions.swap(rhs._definitions); }
    bool empty() const { return _definitions.empty(); }

    bool add(const DefSite &ds, NodeT *node) {
        // if the offset is unknown, make it 0, so that the
        // definition get stretched over all possible offsets
        Offset start, end;
        std::tie(start, end) = getInterval(ds);
        return _definitions[ds.target].add(start, end, node);
    }

    bool addAll(NodeT *node) {
        bool changed = false;
        for (auto &it : _definitions) {
            changed |= it.second.addAll(node);
        }
        return changed;
    }

    bool update(const DefSite &ds, NodeT *node) {
        Offset start, end;
        std::tie(start, end) = getInterval(ds);
        return _definitions[ds.target].update(start, end, node);
    }

    template <typename ContainerT>
    bool add(const DefSite &ds, const ContainerT &nodes) {
        bool changed = false;
        for (auto *n : nodes)
            changed |= add(ds, n);
        return changed;
    }

    template <typename ContainerT>
    bool add(const ContainerT &defsites, NodeT *n) {
        bool changed = false;
        for (auto &ds : defsites)
            changed |= add(ds, n);
        return changed;
    }

    bool add(NodeT *target, const OffsetsT &elems) {
        bool changed = false;
        for (auto &it : elems)
            changed |= _definitions[target].add(it.first, it.second);
        return changed;
    }

    bool add(const DefinitionsMap<NodeT> &rhs) {
        bool changed = false;
        for (auto &it : rhs) {
            changed |= add(it.first, it.second);
        }
        return changed;
    }

    bool update(const DefSite &ds, const std::vector<NodeT *> &nodes) {
        bool changed = false;
        for (auto n : nodes)
            changed |= update(ds, n);
        return changed;
    }

    ///
    // Get definitions of the memory described by 'ds'
    std::set<NodeT *> get(const DefSite &ds) {
        auto it = _definitions.find(ds.target);
        if (it == _definitions.end())
            return {};

        Offset start, end;
        std::tie(start, end) = getInterval(ds);
        return it->second.gather(start, end);
    }

    ///
    // Return intervals of bytes from 'ds' that are not defined by this map
    std::vector<IntervalT> undefinedIntervals(const DefSite &ds) const {
        auto it = _definitions.find(ds.target);
        if (it == _definitions.end())
            return {IntervalT(ds.offset, ds.offset + (ds.len - 1))};

        Offset start, end;
        std::tie(start, end) = getInterval(ds);
        return it->second.uncovered(start, end);
    }

    bool definesTarget(NodeT *target) const {
        return _definitions.find(target) != _definitions.end();
    }

    /*
    template <typename KeyFilt, typename SetFilt>
    DefinitionsMap<NodeT> filter(KeyFilt keyfilt, SetFilt setfilt) {
        DefinitionsMap<NodeT> tmp;
        for (auto& it : _definitions) {
            if (keyfilt(it.first) && setfilt(it.second)) {
                tmp._definitions.emplace(it.first, it.second);
            }
        }
        return tmp;
    }
    */

    template <typename FiltFun>
    DefinitionsMap<NodeT> filter(FiltFun filt) {
        DefinitionsMap<NodeT> tmp;
        for (auto &it : _definitions) {
            if (filt(it.first)) {
                tmp._definitions.emplace(it.first, it.second);
            }
        }
        return tmp;
    }

    DefinitionsMap<NodeT> intersect(const DefinitionsMap<NodeT> &rhs) {
        DefinitionsMap<NodeT> retval;
        for (auto &it : _definitions) {
            auto rhsit = rhs._definitions.find(it.first);
            if (rhsit != rhs._definitions.end()) {
                retval.add(it.first, it.second.intersection(rhsit->second));
            }
        }
        return retval;
    }

    // FIXME: do that as iterators
    std::set<NodeT *> values() const {
        std::set<NodeT *> ret;
        for (auto &it : _definitions) {
            for (auto &it2 : it.second) {
                ret.insert(it2.second.begin(), it2.second.end());
            }
        }
        return ret;
    }

    auto begin() const -> decltype(_definitions.begin()) {
        return _definitions.begin();
    }

    auto end() const -> decltype(_definitions.end()) {
        return _definitions.end();
    }

    bool operator==(const DefinitionsMap<NodeT> &oth) const {
        return _definitions == oth._definitions;
    }

    size_t size() const { return _definitions.size(); }

#ifndef NDEBUG
    void dump() const {
        for (auto &it : _definitions) {
            it.first->dump();
            std::cout << " defined at ";
            it.second.dump();
        }
    }
#endif
};

} // namespace dda
} // namespace dg

#endif
