#ifndef DG_INTERVALS_LIST_H_
#define DG_INTERVALS_LIST_H_

#include "dg/Offset.h"
#include <cassert>
#include <list>

namespace dg {
namespace dda {

class IntervalsList {
    struct Interval {
        Offset start;
        Offset end;

        Interval(Offset s, Offset e) : start(s), end(e) {
            assert(start <= end);
        }

        Interval(const std::pair<Offset, Offset> &I)
                : Interval(I.first, I.second) {}

        bool overlaps(const Interval &I) const {
            return start <= I.end && end >= I.start;
        }
        Offset length() const { return end - start + 1; }
    };

    std::list<Interval> intervals;

    template <typename Iterator>
    void _replace_overlapping(const Interval &I, Iterator it, Iterator to) {
        assert(it->overlaps(I) && to->overlaps(I));
        assert(it != intervals.end());
        assert(to != intervals.end());
        it->start = std::min(it->start, I.start);
        it->end = std::max(to->end, I.end);

        ++it;
        ++to;
        if (it != intervals.end()) {
            intervals.erase(it, to);
        } else {
            assert(to == intervals.end());
        }
    }

#ifndef NDEBUG
    bool _check() {
        auto it = intervals.begin();
        if (it != intervals.end()) {
            assert(it->start < it->end);
            auto last = it++;
            while (it != intervals.end()) {
                assert(last->start < last->end);
                assert(last->end < it->start);
                assert(it->start < it->end);
            }
        }
        return true;
    }
#endif // NDEBUG

  public:
    void add(Offset start, Offset end) { add({start, end}); }

    void add(const Interval &I) {
        if (intervals.empty())
            intervals.push_back(I);

        auto it = intervals.begin();
        auto end = intervals.end();

        while (it != end) {
            if (I.overlaps(*it)) {
                auto to = ++it;
                while (to != end && I.overlaps(*to)) {
                    ++to;
                }
                if (to == end) {
                    --to;
                }
                _replace_overlapping(I, it, to);
                break;
            }
            if (it->start > I.end) {
                intervals.insert(it, I);
                break;
            }

            ++it;
        }

        if (it == end) {
            intervals.push_back(I);
        }
        assert(_check());
    }

    IntervalsList &intersectWith(const IntervalsList &rhs) {
        if (intervals.empty())
            return *this;

        auto it = intervals.begin();
        for (const auto &RI : rhs.intervals) {
            while (it->end < RI.start) {
                auto tmp = it++;
                intervals.erase(tmp);
                if (it == intervals.end()) {
                    return *this;
                }
            }
            if (it->overlaps(RI)) {
                it->start = std::max(it->start, RI.start);
                it->end = std::min(it->end, RI.end);
            }
        }

        return *this;
    }

    auto begin() -> decltype(intervals.begin()) { return intervals.begin(); }
    auto begin() const -> decltype(intervals.begin()) {
        return intervals.begin();
    }
    auto end() -> decltype(intervals.end()) { return intervals.end(); }
    auto end() const -> decltype(intervals.end()) { return intervals.end(); }
};

} // namespace dda
} // namespace dg

#endif
