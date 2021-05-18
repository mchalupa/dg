#ifndef DG_DISJUNCTIVE_INTERVAL_MAP_H_
#define DG_DISJUNCTIVE_INTERVAL_MAP_H_

#include <algorithm>
#include <cassert>
#include <map>
#include <set>
#include <vector>

#ifndef NDEBUG
#include <iostream>
#endif

#include "dg/Offset.h"

namespace dg {
namespace ADT {

template <typename T = int64_t>
struct DiscreteInterval {
    using ValueT = T;

    T start;
    T end;

    DiscreteInterval(T s, T e) : start(s), end(e) {
        assert(s <= e && "Invalid interval");
    }

    T length() const {
        // +1 as the intervals are discrete
        // (interval |0|1|2|3|  has length 4)
        return end - start + 1;
    }

    // total order on intervals so that we can insert them
    // to std containers. We want to compare them only
    // according to the start value.
    bool operator<(const DiscreteInterval &I) const { return start < I.start; }

    bool operator==(const DiscreteInterval &I) const {
        return start == I.start && end == I.end;
    }

    bool operator!=(const DiscreteInterval &I) const { return !operator==(I); }

    bool overlaps(const DiscreteInterval &I) const {
        return (start <= I.start && end >= I.start) || I.end >= start;
    }

    bool covers(const DiscreteInterval &I) const {
        return (start <= I.start && end >= I.end);
    }

    bool overlaps(T rhs_start, T rhs_end) const {
        return overlaps(DiscreteInterval(rhs_start, rhs_end));
    }

    bool covers(T rhs_start, T rhs_end) const {
        return covers(DiscreteInterval(rhs_start, rhs_end));
    }

#ifndef NDEBUG
    void dump() const;
#endif
};

///
// Mapping of disjunctive discrete intervals of values
// to sets of ValueT.
template <typename ValueT, typename IntervalValueT = Offset>
class DisjunctiveIntervalMap {
  public:
    using IntervalT = DiscreteInterval<IntervalValueT>;
    using ValuesT = std::set<ValueT>;
    using MappingT = std::map<IntervalT, ValuesT>;
    using iterator = typename MappingT::iterator;
    using const_iterator = typename MappingT::const_iterator;

    ///
    // Return true if the mapping is updated anyhow
    // (intervals split, value added).
    bool add(const IntervalValueT start, const IntervalValueT end,
             const ValueT &val) {
        return add(IntervalT(start, end), val);
    }

    bool add(const IntervalT &I, const ValueT &val) {
        return _add(I, val, false);
    }

    template <typename ContT>
    bool add(const IntervalT &I, const ContT &vals) {
        bool changed = false;
        for (const ValueT &val : vals) {
            changed |= _add(I, val, false);
        }
        return changed;
    }

    bool update(const IntervalValueT start, const IntervalValueT end,
                const ValueT &val) {
        return update(IntervalT(start, end), val);
    }

    bool update(const IntervalT &I, const ValueT &val) {
        return _add(I, val, true);
    }

    template <typename ContT>
    bool update(const IntervalT &I, const ContT &vals) {
        bool changed = false;
        for (const ValueT &val : vals) {
            changed |= _add(I, val, true);
        }
        return changed;
    }

    // add the value 'val' to all intervals
    bool addAll(const ValueT &val) {
        bool changed = false;
        for (auto &it : _mapping) {
            changed |= it.second.insert(val).second;
        }
        return changed;
    }

    // return true if some intervals from the map
    // has a overlap with I
    bool overlaps(const IntervalT &I) const {
        if (_mapping.empty())
            return false;

        auto ge = _find_ge(I);
        if (ge == _mapping.end()) {
            auto last = _get_last();
            return last->first.end >= I.start;
        }
        return ge->first.start <= I.end;
    }

    bool overlaps(IntervalValueT start, IntervalValueT end) const {
        return overlaps(IntervalT(start, end));
    }

    // return true if the map has an entry for
    // each single byte from the interval I
    bool overlapsFull(const IntervalT &I) const {
        if (_mapping.empty()) {
            return false;
        }

        auto ge = _find_ge(I);
        if (ge == _mapping.end()) {
            auto last = _get_last();
            return last->first.end >= I.end;
        }
        if (ge->first.start > I.start) {
            if (ge == _mapping.begin())
                return false;
            auto prev = ge;
            --prev;
            if (prev->first.end != ge->first.start - 1)
                return false;
        }

        IntervalValueT last_end = ge->first.end;
        while (ge->first.end < I.end) {
            ++ge;
            if (ge == _mapping.end())
                return false;

            if (ge->first.start != last_end + 1)
                return false;

            last_end = ge->first.end;
        }

        // full overlap means that there are not uncovered bytes
        assert(uncovered(I).empty());
        return true;
    }

    bool overlapsFull(IntervalValueT start, IntervalValueT end) const {
        return overlapsFull(IntervalT(start, end));
    }

    DisjunctiveIntervalMap
    intersection(const DisjunctiveIntervalMap &rhs) const {
        DisjunctiveIntervalMap tmp;
        // FIXME: this could be done more efficiently
        auto it = _mapping.begin();
        for (auto &rhsit : rhs._mapping) {
            while (it->first.end < rhsit.first.start) {
                if (it == _mapping.end()) {
                    return tmp;
                }
            }
            if (it->first.overlaps(rhsit.first)) {
                decltype(rhsit.second) vals;
                std::set_intersection(it->second.begin(), it->second.end(),
                                      rhsit.second.begin(), rhsit.second.end(),
                                      std::inserter(vals, vals.begin()));
                tmp.add(IntervalT{std::max(it->first.start, rhsit.first.start),
                                  std::min(it->first.end, rhsit.first.end)},
                        vals);
            }
        }
        return tmp;
    }

    ///
    // Gather all values that are covered by the interval I
    std::set<ValueT> gather(IntervalValueT start, IntervalValueT end) const {
        return gather(IntervalT(start, end));
    }

    std::set<ValueT> gather(const IntervalT &I) const {
        std::set<ValueT> ret;

        auto it = le(I);
        if (it == end())
            return ret;

        assert(it->first.overlaps(I) && "The found interval should overlap");
        while (it != end() && it->first.overlaps(I)) {
            ret.insert(it->second.begin(), it->second.end());
            ++it;
        }

        return ret;
    }

    std::vector<IntervalT> uncovered(IntervalValueT start,
                                     IntervalValueT end) const {
        return uncovered(IntervalT(start, end));
    }

    std::vector<IntervalT> uncovered(const IntervalT &I) const {
        if (_mapping.empty())
            return {I};

        auto it = le(I);
        if (it == end())
            return {I};

        std::vector<IntervalT> ret;
        IntervalT cur = I;

        if (cur.start > it->first.start) {
            // the first interval covers the whole I?
            if (it->first.end >= I.end) {
                return {};
            }

            assert(cur == I);
            cur.start = it->first.end + 1;
            assert(cur.end == I.end);
            assert(I.end > it->first.end);
            // we handled this interval, move further
            ++it;

            // nothing else to handle...
            if (it == _mapping.end()) {
                if (cur.start <= cur.end)
                    return {cur};
            }
        }

        while (true) {
            assert(cur.start <= it->first.start);
            if (cur.start != it->first.start && cur.start < it->first.start) {
                assert(it->first.start != 0 && "Underflow");
                ret.push_back(IntervalT{cur.start, it->first.start - 1});
            }
            // does the rest of the interval covers all?
            if (it->first.end >= I.end)
                break;

            assert(it->first.end != (~static_cast<IntervalValueT>(0)) &&
                   "Overflow");
            cur.start = it->first.end + 1;
            assert(cur.end == I.end);

            ++it;
            if (it == end()) {
                if (cur.start <= cur.end)
                    ret.push_back(cur);
                // we're done here
                break;
            }
        }

        return ret;
    }

    bool empty() const { return _mapping.empty(); }
    size_t size() const { return _mapping.size(); }

    iterator begin() { return _mapping.begin(); }
    const_iterator begin() const { return _mapping.begin(); }
    iterator end() { return _mapping.end(); }
    const_iterator end() const { return _mapping.end(); }

    bool operator==(
            const DisjunctiveIntervalMap<ValueT, IntervalValueT> &rhs) const {
        return _mapping == rhs._mapping;
    }

    // return the iterator to an element that is the first
    // that overlaps the interval I or end() if there is
    // no such interval
    iterator le(const IntervalT &I) { return _shift_le(_find_ge(I), I); }

    const_iterator le(const IntervalT &I) const {
        return _shift_le(_find_ge(I), I);
    }

    iterator le(const IntervalValueT start, const IntervalValueT end) {
        return le(IntervalT(start, end));
    }

    const_iterator le(const IntervalValueT start,
                      const IntervalValueT end) const {
        return le(IntervalT(start, end));
    }

#ifndef NDEBUG
    friend std::ostream &
    operator<<(std::ostream &os,
               const DisjunctiveIntervalMap<ValueT, IntervalValueT> &map) {
        os << "{";
        for (const auto &pair : map) {
            if (pair.second.empty())
                continue;

            os << "{ ";
            os << pair.first.start << "-" << pair.first.end;
            os << ": " << *pair.second.begin();
            os << " }, ";
        }
        os << "}";
        return os;
    }

    void dump() const { std::cout << *this << "\n"; }
#endif

#if 0
    friend llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const DisjunctiveIntervalMap<ValueT, IntervalValueT>& map) {
        os << "{";
        for (const auto& pair : map) {
            if (pair.second.empty())
                continue;

            os << "{ ";
            os << *pair.first.start << "-" << *pair.first.end;
            os << ": " << *pair.second.begin();
            os << " }, ";
        }
        os << "}";
        return os;
    }

#endif

  private:
    // shift the iterator such that it will point to the
    // first element that overlaps with I, or to end
    // if there is no such interval
    template <typename IteratorT>
    IteratorT _shift_le(const IteratorT &startge, const IntervalT &I) const {
        // find the element that starts at the same value
        // as I or later (i.e. I.start >= it.start)
        if (startge == end()) {
            auto last = _get_last();
            if (last->first.end >= I.start) {
                assert(last->first.overlaps(I));
                return last;
            }

            return end();
        }

        assert(startge->first.start >= I.start);

        // check whether there's
        // an previous interval with an overlap
        if (startge != begin()) {
            auto tmp = startge;
            --tmp;
            if (tmp->first.end >= I.start) {
                assert(tmp->first.overlaps(I));
                return tmp;
            }
            // fall-through
        }

        // starge is the first interval or the
        // previous interval does not overlap.
        // Just check whether this interval overlaps
        if (startge->first.start > I.end)
            return end();

        assert(startge->first.overlaps(I));
        return startge;
    }

    // Split interval [a,b] to two intervals [a, where] and [where + 1, b].
    // Each of the new intervals has a copy of the original set associated
    // to the original interval.
    // Returns the new lower interval
    // XXX: could we pass just references to the iterator?
    template <typename IteratorT, typename HintIteratorT>
    IteratorT splitIntervalHint(IteratorT I, IntervalValueT where,
                                HintIteratorT hint) {
        auto interval = I->first;
        auto values = std::move(I->second);

        assert(interval.start != interval.end && "Cannot split such interval");
        assert(interval.start <= where && where <= interval.end &&
               "Value 'where' must lie inside the interval");
        assert(where < interval.end && "The second interval would be empty");

        // remove the original interval and replace it with
        // two splitted intervals
        _mapping.erase(I);
        auto ret = _mapping.emplace_hint(hint, IntervalT(interval.start, where),
                                         values);
        _mapping.emplace_hint(hint, IntervalT(where + 1, interval.end),
                              std::move(values));
        return ret;
    }

    bool splitExtBorders(const IntervalT &I) {
        assert(!_mapping.empty());
        bool changed = false;

        auto ge = _mapping.lower_bound(I);
        if (ge == _mapping.end()) {
            // the last interval must start somewhere to the
            // left from our new interval
            auto last = _get_last();
            assert(last->first.start < I.start);

            if (last->first.end > I.end) {
                last = splitIntervalHint(last, I.end, ge);
                changed |= true;
            }

            if (last->first.end >= I.start) {
                splitIntervalHint(last, I.start - 1, ge);
                changed |= true;
            }
            return changed;
        }

        // we found an interval starting at I.start
        // or later
        assert(ge->first.start >= I.start);
        // FIXME: optimize this...
        //  add _find_le to find the "closest" interval from the right
        //  (maybe iterating like this is faster, though)
        auto it = ge;
        while (it->first.start <= I.end) {
            if (it->first.end > I.end) {
                it = splitIntervalHint(it, I.end, _mapping.end());
                changed = true;
                break;
            }
            ++it;
            if (it == _mapping.end())
                break;
        }

        // we may have also overlap from the previous interval
        if (changed)
            ge = _mapping.lower_bound(I);
        if (ge != _mapping.begin()) {
            auto prev = ge;
            --prev;
            auto prev_end = prev->first.end;
            if (prev_end >= I.start) {
                ge = splitIntervalHint(prev, I.start - 1, ge);
                changed = true;
            }
            // is the new interval entirely covered by the previous?
            if (prev_end > I.end) {
                // get the higher of the new intervals
                ++ge;
                assert(ge != _mapping.end());
                assert(ge->first.end == prev_end);
#ifndef NDEBUG
                auto check =
#endif
                        splitIntervalHint(ge, I.end, _mapping.end());
                assert(check->first == I);
                changed = true;
            }
        }

        return changed;
    }

    template <typename IteratorT>
    bool _addValue(IteratorT I, ValueT val, bool update) {
        if (update) {
            if (I->second.size() == 1 && I->second.count(val) > 0)
                return false;

            I->second.clear();
            I->second.insert(val);
            return true;
        }

        return I->second.insert(val).second;
    }

    // If the boolean 'update' is set to true, the value
    // is not added, but rewritten
    bool _add(const IntervalT &I, const ValueT &val, bool update = false) {
        if (_mapping.empty()) {
            _mapping.emplace(I, ValuesT{val});
            return true;
        }

        // fast path
        // auto fastit = _mapping.lower_bound(I);
        // if (fastit != _mapping.end() &&
        //    fastit->first == I) {
        //    return _addValue(fastit, val, update);
        //}

        // XXX: pass the lower_bound iterator from the fast path
        // so that we do not search the mapping again
        bool changed = splitExtBorders(I);
        _check();

        // splitExtBorders() arranged the intervals such
        // that some interval starts with ours
        // and some interval ends with ours.
        // Now we just create new intervals in the gaps
        // and add values to the intervals that we have

        // FIXME: splitExtBorders() can return the iterator,
        // so that we do not need to search the map again.
        auto it = _find_ge(I);
        assert(!changed || it != _mapping.end());

        // we do not have any overlapping interval
        if (it == _mapping.end() || I.end < it->first.start) {
            assert(!overlaps(I) && "Bug in add() or in overlaps()");
            _mapping.emplace(I, ValuesT{val});
            return true;
        }

        auto rest = I;
        assert(rest.end >= it->first.start); // we must have some overlap here
        while (it != _mapping.end()) {
            if (rest.start < it->first.start) {
                // add the gap interval
                _mapping.emplace_hint(
                        it, IntervalT(rest.start, it->first.start - 1),
                        ValuesT{val});
                rest.start = it->first.start;
                changed = true;
            } else {
                // update the existing interval and shift
                // to the next interval
                assert(rest.start == it->first.start);
                changed |= _addValue(it, val, update);
                if (it->first.end == rest.end)
                    break;

                rest.start = it->first.end + 1;
                ++it;

                // our interval spans to the right
                // after the last covered interval
                if (it == _mapping.end() || it->first.start > rest.end) {
                    _mapping.emplace_hint(it, rest, ValuesT{val});
                    changed = true;
                    break;
                }
            }
        }

        _check();
        return changed;
    }

    // find the elements starting at
    // or right to the interval
    typename MappingT::iterator _find_ge(const IntervalT &I) {
        // lower_bound = lower upper bound
        return _mapping.lower_bound(I);
    }

    typename MappingT::const_iterator _find_ge(const IntervalT &I) const {
        return _mapping.lower_bound(I);
    }

    typename MappingT::iterator _get_last() {
        assert(!_mapping.empty());
        return (--_mapping.end());
    }

    typename MappingT::const_iterator _get_last() const {
        assert(!_mapping.empty());
        return (--_mapping.end());
    }

    void _check() const {
#ifndef NDEBUG
        // check that the keys are disjunctive
        auto it = _mapping.begin();
        auto last = it->first;
        ++it;
        while (it != _mapping.end()) {
            assert(last.start <= last.end);
            // this one is nontrivial (the other assertions
            // should be implied by the Interval and std::map propertis)
            assert(last.end < it->first.start);
            assert(it->first.start <= it->first.end);

            last = it->first;
            ++it;
        }
#endif // NDEBUG
    }

    MappingT _mapping;
};

} // namespace ADT
} // namespace dg

#endif // _DG_DISJUNCTIVE_INTERVAL_MAP_H_
