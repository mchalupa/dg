#ifndef _DG_INTERVALSET_H
#define _DG_INTERVALSET_H

#include "analysis/Offset.h"
#include "analysis/ReachingDefinitions/RDMap.h"

namespace dg {
namespace analysis {
namespace rd {
namespace srg {
namespace detail {

/**
 * represents a both-side closed interval of values
 */
class Interval {
    using T = Offset;

    T start;
    T len;

    static inline T min(T a, T b) {
        return a < b ? a : b;
    }

    static inline T max(T a, T b) {
        return a > b ? a : b;
    }

public:
    Interval(T start, T len): start(start), len(len) {}

    bool unknown() const {
        return start.isUnknown() || len.isUnknown();
    }

    bool overlaps(const Interval& other) const {
        if (unknown() || other.unknown()) {
            return true;
        }
        return intervalsOverlap(start.offset, len.offset, other.start.offset, other.len.offset);
    }

    bool isSubsetOf(const Interval& other) const {
        if (unknown() || other.unknown()) {
            return true;
        }
        return start >= other.start && start + len <= other.start + other.len;
    }

    bool unite(const Interval& other) {
        if (overlaps(other) || start + len == other.start || other.start + other.len == start) {
            T a = min(start, other.start);
            T b = max(start + len, other.start + other.len);
            len = b - a;
            start = a;
            return true;
        }
        return false;
    }

    T getStart() const {
        return start;
    }
    
    T getLength() const {
        return len;
    }

};

class DisjointIntervalSet {
    std::vector<Interval> intervals;
public:

    DisjointIntervalSet() { }

    template <typename C>
    DisjointIntervalSet(const C& c) {
        for (const Interval& interval : c) {
            insert(interval);
        }
    }

    void insert(Interval interval) {
        // find & remove all overlapping intervals
        for (auto it = begin(); it != end(); ) {
            if (interval.unite(*it)) {
                it = intervals.erase(it);
            } else {
                ++it;
            }
        }

        intervals.push_back(std::move(interval));
    }

    auto cbegin() const -> decltype(intervals.begin()) {
        return intervals.begin();
    }

    auto begin() -> decltype(intervals.begin()) {
        return intervals.begin();
    }

    auto end() -> decltype(intervals.end()) {
        return intervals.end();
    }

    auto cend() const -> decltype(intervals.cend()) {
        return intervals.cend();
    }

    auto size() -> decltype(intervals.size()) {
        return intervals.size();
    }

    const std::vector<Interval>& toVector() const {
        return intervals;
    }

    std::vector<Interval>&& moveVector() {
        return std::move(intervals);
    }
};

/**
 * Sorted mapping of intervals to values.
 * Useful for mapping range of defined memory to node that defined the range.
 *
 * Template parameters:
 *  V - type of value stored against interval
 *  ReverseLookup - order of interval lookup in collect. 
 *          If ReverseLookup=true, search will start at the end, so last values will be returned first
 */
template <typename V, bool ReverseLookup=true>
class IntervalMap {

    std::vector<std::pair<Interval, V>> buckets;

    /**
     * Returns true if @interval is subset of union of intervals in @intervals
     */
    static inline bool isCovered(const Interval& interval, const DisjointIntervalSet& intervals) {
        for (auto it = intervals.cbegin(); it != intervals.cend(); ++it) {
            if (interval.overlaps(*it) && interval.isSubsetOf(*it)) {
                return true;
            }
        }
        return false;
    }

public:
    void add(Interval&& interval, const V& value) {
        //                                    move interval, copy value
        auto to_add = std::pair<Interval, V>(std::move(interval), value);
        buckets.push_back(std::move(to_add));
    }

    /**
     * Returns set of values such, that @interval is subset of union of all their key intervals.
     * If ReverseLookup, then searching starts at the end of IntervalMap.
     * Searching stops as soon as satisfying set is found
     *
     * Return Tuple:
     *      0 - values associated with key intervals
     *      1 - key intervals that (partially) cover specified interval
     *      2 - true if @interval is fully covered by returned key interval set, false otherwise
     */
    std::tuple<std::vector<V>, std::vector<Interval>, bool> 
        collect(const Interval& interval, const std::vector<detail::Interval>& covered) const {

        std::vector<V> result;
        DisjointIntervalSet intervals = covered;
        bool is_covered = false;

        static_assert(ReverseLookup, "forward lookup in IntervalMap is not yet supported");
        for (auto it = buckets.rbegin(); !is_covered && it != buckets.rend(); ++it) {
            if (it->first.overlaps(interval) && !isCovered(it->first, intervals)) {
                intervals.insert(it->first);
                result.push_back(it->second);
                is_covered = isCovered(interval, intervals);
            }
        }

        return std::tuple<std::vector<V>, std::vector<Interval>, bool>(std::move(result), std::move(intervals.moveVector()), is_covered);
    }

};

}
}
}
}
}
#endif /* _DG_INTERVALSET_H */
