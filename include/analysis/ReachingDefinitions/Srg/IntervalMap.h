#ifndef _DG_INTERVALSET_H
#define _DG_INTERVALSET_H

#include <algorithm>
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

public:
    Interval(T start, T len): start(start), len(len) {}

    bool isUnknown() const {
        return start.isUnknown() || len.offset == 0;
    }

    bool overlaps(const Interval& other) const {
        if (isUnknown() || other.isUnknown())
            return false;
        return intervalsOverlap(start.offset, len.offset, other.start.offset, other.len.offset);
    }

    bool isSubsetOf(const Interval& other) const {
        return (start >= other.start) && (start + len <= other.start + other.len);
    }

    bool unite(const Interval& other) {
        using std::min;
        using std::max;

        if (isUnknown() || other.isUnknown())
            return false;
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

/**
 * Represents a set of disjoint intervals.
 * DisjointIntervalSet::insert is the main functions, other functions are delegated to the underlying vector.
 */
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

    /**
     * Inserts @interval to the set, maintaining the invariant that all intervals
     * in the set are disjoint. That is, if @interval overlaps with @x, that is already in the set,
     * @x is removed, @interval is united with @x and inserted to the set.
     */

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
        // it could be, that would be an under-approximation
        if (interval.isUnknown()) {
            return true;
        }
        for (auto it = intervals.cbegin(); it != intervals.cend(); ++it) {
            if (interval.overlaps(*it) && interval.isSubsetOf(*it)) {
                return true;
            }
        }
        return false;
    }

public:

    /**
     * Modifies this interval map in a way such, that
     * collecting using @ki or any of its sub-interval will produce empty vector.
     * Existing intervals that intersect with @ki will have that intersection removed
     * (possibly splitting up one interval into two)
     */
    void killOverlapping(const Interval& ki) {
        if (ki.isUnknown())
            return;

        std::vector<std::pair<Interval, V>> to_add;
        for (auto&& it = buckets.begin(); it != buckets.end(); ) {
            Interval& interval = it->first;
            V& v = it->second;

            if (!interval.getLength().isUnknown() && !interval.isUnknown() && interval.overlaps(ki)) {
                if (ki.isSubsetOf(interval)) {
                    // @interval is split into 2 by @ki

                    // calculate the left part
                    Offset start = interval.getStart();
                    Offset end = ki.getStart();
                    auto new_int = Interval{start, end - start};
                    if (new_int.getLength().offset > 0) {
                        to_add.push_back(std::pair<Interval, V>(std::move(new_int), std::move(v)));
                    }
                    // calculate the right part
                    start = ki.getStart() + ki.getLength();
                    end = interval.getStart() + interval.getLength();
                    new_int = Interval{start, end - start};
                    if (new_int.getLength().offset > 0) {
                        to_add.push_back(std::pair<Interval, V>(std::move(new_int), std::move(v)));
                    }
                } else if (!ki.isSubsetOf(interval) && !interval.isSubsetOf(ki)) {
                    // calculate preserved interval
                    Offset start, end;
                    if (ki.getStart() <= interval.getStart()) {
                        // ki is on the left
                        start = ki.getStart() + ki.getLength();
                        end = interval.getStart() + interval.getLength();
                    } else {
                        // ki is on the right
                        start = interval.getStart();
                        end = ki.getStart();
                    }
                    auto new_int = Interval{start, end - start};
                    if (new_int.getLength().offset > 0) {
                        to_add.push_back(std::pair<Interval, V>(std::move(new_int), std::move(v)));
                    }
                } // else kill the whole interval, which is done by erasing it from buckets vector
                it = buckets.erase(it);
            } else {
                ++it;
            }
        }
        std::move(to_add.begin(), to_add.end(), std::back_inserter(buckets));
    }

    /**
     * Adds a new mapping from @interval to @value.
     */
    void add(Interval&& interval, const V& value) {
        //                                    move interval, copy value
        auto to_add = std::make_pair<Interval, V>(std::move(interval), V(value));
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
        for (auto it = buckets.rbegin(); it != buckets.rend(); ++it) {
            if (interval.isUnknown() || it->first.isUnknown() || (it->first.overlaps(interval))) {
                intervals.insert(it->first);
                result.push_back(it->second);
                is_covered = isCovered(interval, intervals);
            }
        }
        is_covered = isCovered(interval, intervals);

        return std::tuple<std::vector<V>, std::vector<Interval>, bool>(std::move(result), std::move(intervals.moveVector()), is_covered);
    }

    /**
     * Returns set of all values such, that @interval intersects with
     * interval associated with each of values
     */
    std::vector<V> collectAll(const Interval& interval) const {
        std::vector<V> result;

        static_assert(ReverseLookup, "forward lookup in IntervalMap is not yet supported");
        for (auto it = buckets.rbegin(); it != buckets.rend(); ++it) {
            if (interval.isUnknown() || it->first.isUnknown() || it->first.overlaps(interval)) {
                result.push_back(it->second);
            }
        }
        return result;
    }

};

}
}
}
}
}
#endif /* _DG_INTERVALSET_H */
