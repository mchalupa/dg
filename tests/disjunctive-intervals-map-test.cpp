#include "catch.hpp"

#include <random>
#include <sstream>
#include <vector>

#undef NDEBUG

#include "dg/Offset.h"
#include "dg/ADT/DisjunctiveIntervalMap.h"

using namespace dg;
using dg::ADT::DisjunctiveIntervalMap;

static std::ostream& operator<<(std::ostream& os, const std::vector<std::tuple<int,int,int>>& v) {
    os << "{";
    for (const auto& tup : v) {
        os << "{ ";
        os << std::get<0>(tup) << "-";
        os << std::get<1>(tup) << ": ";
        os << std::get<2>(tup);
        os << " }, ";
    }
    os << "}";
    return os;
}


template<typename ValueT, typename IntervalValueT>
class DisjunctiveIntervalMapMatcher : public Catch::MatcherBase<DisjunctiveIntervalMap<ValueT, IntervalValueT>> {
    std::vector<std::tuple<IntervalValueT, IntervalValueT, ValueT>> structure;
public:
    DisjunctiveIntervalMapMatcher(std::vector<std::tuple<IntervalValueT, IntervalValueT, ValueT>> s) : structure(std::move(s)) { }

    bool match(const DisjunctiveIntervalMap<ValueT, IntervalValueT>& M) const override {
        if (M.size() != structure.size()) {
            return false;
        }

        size_t i = 0;
        for (const auto& pair : M) {
            const auto& interval = pair.first;
            const auto& values = pair.second;

            if(interval.start != std::get<0>(structure[i])) {
                return false;
            }

            if(interval.end != std::get<1>(structure[i])) {
                return false;
            }

            if(values.find(std::get<2>(structure[i])) == values.end()) {
                return false;
            }

            ++i;
        }
        return true;
    }

    virtual std::string describe() const override {
        std::ostringstream ss;
        ss << "has the structure: " << structure;
        return ss.str();
    }
};

static inline DisjunctiveIntervalMapMatcher<int, int> HasStructure(std::initializer_list<std::tuple<int, int, int>> structure) {
    return DisjunctiveIntervalMapMatcher<int, int>(structure);
}

TEST_CASE("Querying empty set", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    REQUIRE(M.empty());
}

TEST_CASE("Add same", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,2, 1);
    REQUIRE(M.size() == 1);
    REQUIRE(M.overlaps(0,0));
    REQUIRE(M.overlaps(0,1));
    REQUIRE(M.overlaps(0,2));
    REQUIRE(M.overlaps(1,1));
    REQUIRE(M.overlaps(1,2));
    REQUIRE(M.overlaps(2,2));
    REQUIRE(M.overlapsFull(0,0));
    REQUIRE(M.overlapsFull(0,1));
    REQUIRE(M.overlapsFull(0,2));
    REQUIRE(M.overlapsFull(1,1));
    REQUIRE(M.overlapsFull(1,2));
    REQUIRE(M.overlapsFull(2,2));
    REQUIRE(M.overlapsFull(0,2));

    REQUIRE(M.overlaps(0,3));
    REQUIRE(M.overlaps(1,3));
    REQUIRE(M.overlaps(2,3));
    REQUIRE(!M.overlaps(3,3));
    REQUIRE(!M.overlapsFull(0,3));
    REQUIRE(!M.overlapsFull(1,3));
    REQUIRE(!M.overlapsFull(2,3));
    REQUIRE(!M.overlapsFull(3,3));

    REQUIRE(!M.overlapsFull(0,10));
    M.add(0,2, 1);
    REQUIRE(M.size() == 1);
}

TEST_CASE("Add non-overlapping", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,2, 1);
    REQUIRE(M.size() == 1);
    REQUIRE(!M.overlaps(3,4));
    M.add(3,4, 2);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add non-overlapping3", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(3,4, 2);
    REQUIRE(M.size() == 1);
    REQUIRE(M.overlaps(3,4));
    REQUIRE(!M.overlaps(0,2));
    M.add(0,2, 1);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add non-overlapping1", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,10, 1);
    REQUIRE(M.size() == 1);
    REQUIRE(M.overlaps(3,4));
    REQUIRE(M.overlaps(0,0));
    REQUIRE(M.overlaps(0,1));
    REQUIRE(M.overlaps(10,10));
    REQUIRE(M.overlaps(7,15));
    REQUIRE(M.overlaps(0,100));
    REQUIRE(M.overlapsFull(3,4));
    REQUIRE(M.overlapsFull(0,0));
    REQUIRE(M.overlapsFull(0,1));
    REQUIRE(M.overlapsFull(10,10));
    REQUIRE(!M.overlapsFull(0,100));
    REQUIRE(!M.overlaps(11,11));
    REQUIRE(!M.overlaps(11,99));

    M.add(100,101, 2);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add overlapping0", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,2, 1);
    REQUIRE(M.size() == 1);
    REQUIRE(M.overlaps(2,3));
    M.add(2,3, 2);
    REQUIRE(M.size() == 3);
}

TEST_CASE("Add overlapping0com", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(2,3, 2);
    REQUIRE(M.size() == 1);
    REQUIRE(M.overlaps(0,2));
    M.add(0,2, 1);
    REQUIRE(M.size() == 3);
}

TEST_CASE("Add overlapping", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,2, 1);
    REQUIRE(M.size() == 1);
    REQUIRE(M.overlaps(0,4));
    REQUIRE(M.overlapsFull(0,2));
    REQUIRE(!M.overlapsFull(0,4));
    M.add(0,4, 2);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add overlappingCom", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,4, 2);
    REQUIRE(M.size() == 1);
    REQUIRE(M.overlaps(0,2));
    REQUIRE(M.overlapsFull(0,2));
    M.add(0,2, 1);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add overlapping1", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(1,3, 1);
    REQUIRE(M.size() == 1);
    M.add(2,5, 2);
    REQUIRE(M.size() == 3);
}

TEST_CASE("Add overlapping2", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(2,5, 1);
    REQUIRE(M.size() == 1);
    M.add(1,3, 2);
    REQUIRE(M.size() == 3);
}

TEST_CASE("Add overlapping3", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(1,2, 1);
    REQUIRE(M.size() == 1);
    M.add(0,4, 2);
    REQUIRE(M.size() == 3);
}

TEST_CASE("Add overlapping3com", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,4, 2);
    REQUIRE(M.size() == 1);
    M.add(1,2, 1);
    REQUIRE(M.size() == 3);
}

TEST_CASE("Add overlapping5", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,4, 1);
    REQUIRE(M.size() == 1);
    M.add(2,4, 2);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add overlapping5com", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(2,4, 2);
    REQUIRE(M.size() == 1);
    M.add(0,4, 1);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add overlapping4", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,0, 0);
    REQUIRE(M.size() == 1);
    M.add(1,1, 1);
    REQUIRE(M.size() == 2);
    M.add(3,3, 2);
    REQUIRE(M.size() == 3);


    REQUIRE(M.overlapsFull(0,0));
    REQUIRE(M.overlapsFull(0,1));
    REQUIRE(!M.overlapsFull(0,2));
    REQUIRE(!M.overlapsFull(2,3));
    REQUIRE(M.overlapsFull(3,3));
    REQUIRE(!M.overlapsFull(3,5));
    REQUIRE(M.overlaps(3,5));

    M.add(5,5, 3);
    REQUIRE(M.size() == 4);

    REQUIRE(M.overlaps(3,5));
    REQUIRE(M.overlaps(5,5));
    REQUIRE(M.overlapsFull(5,5));

    bool changed = M.add(5,5, 3);
    REQUIRE(changed == false);
    REQUIRE(M.size() == 4);

    M.add(0,10, 4);
    REQUIRE(M.size() == 7);


    REQUIRE(M.overlapsFull(0,0));
    REQUIRE(M.overlapsFull(0,1));
    REQUIRE(M.overlapsFull(0,2));
    REQUIRE(M.overlapsFull(2,3));
    REQUIRE(M.overlapsFull(3,3));
    REQUIRE(M.overlapsFull(3,5));
    REQUIRE(M.overlapsFull(0,5));
    REQUIRE(M.overlapsFull(0,10));
    REQUIRE(!M.overlapsFull(0,11));

    for (int i = 1; i < 11; ++i)
        REQUIRE(!M.overlapsFull(i,11));

    for (int i = 0; i < 11; ++i)
        for (int j = i; j < 11; ++j)
        REQUIRE(M.overlapsFull(i,j));
}

TEST_CASE("Add overlappingX", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,4, 1);
    M.add(1,1, 2);
    M.add(3,5, 3);
    REQUIRE(M.size() == 5);

    REQUIRE(M.overlaps(0,0));
    REQUIRE(M.overlaps(0,10));
    REQUIRE(M.overlaps(0,6));
    REQUIRE(M.overlaps(1,5));

    REQUIRE(M.overlapsFull(0,5));
    REQUIRE(!M.overlapsFull(0,6));
    REQUIRE(M.overlapsFull(1,5));

    using IntervalT = decltype(M)::IntervalT;
    std::vector<IntervalT> results = {
        IntervalT(0,0),
        IntervalT(1,1),
        IntervalT(2,2),
        IntervalT(3,4),
        IntervalT(5,5),
    };

    int i = 0;
    for (auto& I : M) {
        REQUIRE(I.first == results[i++]);
    }
}

TEST_CASE("OverlappsNegative", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int, int> M;
    M.add(0,2, 0);
    REQUIRE(M.overlaps(-1, 5));
    REQUIRE(M.overlaps(-1, 0));
    REQUIRE(M.overlaps(-1, 1));
    REQUIRE(!M.overlaps(-1, -1));
    REQUIRE(!M.overlaps(-4, -1));
    REQUIRE(M.overlaps(-4, 10));
    REQUIRE(!M.overlapsFull(-4, 10));
    REQUIRE(!M.overlapsFull(-1, 0));
    REQUIRE(!M.overlapsFull(-1, 1));
}

TEST_CASE("OverlappsNegative2", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int, int> M;
    M.add(-2,2, 0);
    REQUIRE(M.overlaps(-1, 5));
    REQUIRE(M.overlaps(-1, 0));
    REQUIRE(M.overlaps(-1, 1));
    REQUIRE(M.overlaps(-1, -1));
    REQUIRE(M.overlaps(-4, -1));
    REQUIRE(M.overlaps(-4, 10));
    REQUIRE(!M.overlapsFull(-4, 10));
    REQUIRE(M.overlapsFull(-1, 0));
    REQUIRE(M.overlapsFull(-1, 1));
    REQUIRE(M.overlapsFull(-2, 2));
    REQUIRE(!M.overlapsFull(-2, 3));
    REQUIRE(!M.overlapsFull(-3, 2));
}

TEST_CASE("OverlappsRandom", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int,int> M;
    M.add(0,10, 0);
    REQUIRE(M.size() == 1);

    std::default_random_engine generator;
    std::uniform_int_distribution<int> distribution(-100, 100);

    for (int i = 0; i < 1000; ++i) {
        auto start = distribution(generator);
        auto end = distribution(generator);

        if (end < start)
            std::swap(end, start);

        assert(start <= end);
        if (start >= 0 && start <= 10) {
            REQUIRE(M.overlaps(start, start));
            REQUIRE(M.overlapsFull(start, start));

            REQUIRE(M.overlaps(start, end));
            if (end <= 10)
                REQUIRE(M.overlapsFull(start, end));
        } else {
            REQUIRE(!M.overlaps(start, start));
            REQUIRE(!M.overlapsFull(start, start));

            if (end >= 0 && end <= 10) {
                REQUIRE(M.overlaps(start, end));
                REQUIRE(!M.overlapsFull(start, end));
            } else {
                if (start > 10 || end < 0) {
                    REQUIRE(!M.overlaps(start, end));
                    REQUIRE(!M.overlapsFull(start, end));
                } else {
                    REQUIRE(M.overlaps(start, end));
                    REQUIRE(!M.overlapsFull(start, end));
                }
            }
        }
    }
}

TEST_CASE("OverlapsEmptyNonemptyinterval", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int, int> M;
    REQUIRE_FALSE(M.overlapsFull(0, 10));
    REQUIRE_FALSE(M.overlapsFull(10, 10));
}

TEST_CASE("Split", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int, int> M;

    // add 0-4
    M.update(0,4, 1);

    // now add intervals such that their union is 0-4
    M.update(0,1, 2);
    M.update(1,2, 3);
    M.update(2,2, 4);
    M.update(3,4, 5);

    /*
     * The map should now contain:
     * [0,0] -> 2
     * [1,1] -> 3
     * [2,2] -> 4
     * [3,4] -> 5
     */
    REQUIRE_THAT(M, HasStructure({
        std::make_tuple(0,0, 2),
        std::make_tuple(1,1, 3),
        std::make_tuple(2,2, 4),
        std::make_tuple(3,4, 5)
    }));
}

TEST_CASE("Split2", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int, int> M;

    // add 0-4
    M.update(0,4, 1);

    // now add intervals such that their union is 0-4
    M.update(0,1, 2);
    M.update(1,2, 3);
    M.update(2,3, 4);
    M.update(3,4, 5);

    /*
     * The map should now contain:
     * [0,0] -> 2
     * [1,1] -> 3
     * [2,2] -> 4
     * [3,3] -> 5
     * [4,4] -> 5
     */
    REQUIRE_THAT(M, HasStructure({
        std::make_tuple(0,0, 2),
        std::make_tuple(1,1, 3),
        std::make_tuple(2,2, 4),
        std::make_tuple(3,3, 5),
        std::make_tuple(4,4, 5)
    }));
}

TEST_CASE("Uncovered 1", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    using IntT = decltype(M)::IntervalT;

    auto ret = M.uncovered(2,5);
    REQUIRE(ret.size() == 1);
    REQUIRE(ret[0] == IntT{2,5});

    M.update(0,5, 0);

    ret = M.uncovered(2,5);
    REQUIRE(ret.size() == 0);

    ret = M.uncovered(0,5);
    REQUIRE(ret.size() == 0);

    ret = M.uncovered(3,4);
    REQUIRE(ret.size() == 0);

    ret = M.uncovered(5,5);
    REQUIRE(ret.size() == 0);

    ret = M.uncovered(1,1);
    REQUIRE(ret.size() == 0);

    ret = M.uncovered(0,0);
    REQUIRE(ret.size() == 0);

    ret = M.uncovered(6,6);
    REQUIRE(ret.size() == 1);
    REQUIRE(ret[0] == IntT{6,6});

    ret = M.uncovered(6,10);
    REQUIRE(ret.size() == 1);
    REQUIRE(ret[0] == IntT{6,10});
}

TEST_CASE("Uncovered 2", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    using IntT = decltype(M)::IntervalT;

    M.update(2,5, 0);

    auto ret = M.uncovered(0,5);
    REQUIRE(ret.size() == 1);
    REQUIRE(ret[0] == IntT{0,1});

    ret = M.uncovered(0,10);
    REQUIRE(ret.size() == 2);
    REQUIRE(ret[0] == IntT{0,1});
    REQUIRE(ret[1] == IntT{6,10});
}

TEST_CASE("Uncovered 3", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    using IntT = decltype(M)::IntervalT;

    M.update(0,0, 0);
    M.update(2,2, 0);
    M.update(4,4, 0);
    M.update(6,6, 0);

    auto ret = M.uncovered(0,7);
    REQUIRE(ret.size() == 4);
    REQUIRE(ret[0] == IntT{1,1});
    REQUIRE(ret[1] == IntT{3,3});
    REQUIRE(ret[2] == IntT{5,5});
    REQUIRE(ret[3] == IntT{7,7});

    ret = M.uncovered(0,10);
    REQUIRE(ret.size() == 4);
    REQUIRE(ret[0] == IntT{1,1});
    REQUIRE(ret[1] == IntT{3,3});
    REQUIRE(ret[2] == IntT{5,5});
    REQUIRE(ret[3] == IntT{7,10});
}

TEST_CASE("Uncovered 4", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    using IntT = decltype(M)::IntervalT;

    M.update(1,1, 0);
    M.update(3,3, 0);
    M.update(5,5, 0);
    M.update(7,7, 0);

    auto ret = M.uncovered(0,7);
    REQUIRE(ret.size() == 4);
    REQUIRE(ret[0] == IntT{0,0});
    REQUIRE(ret[1] == IntT{2,2});
    REQUIRE(ret[2] == IntT{4,4});
    REQUIRE(ret[3] == IntT{6,6});

    ret = M.uncovered(0,8);
    REQUIRE(ret.size() == 5);
    REQUIRE(ret[0] == IntT{0,0});
    REQUIRE(ret[1] == IntT{2,2});
    REQUIRE(ret[2] == IntT{4,4});
    REQUIRE(ret[3] == IntT{6,6});
    REQUIRE(ret[4] == IntT{8,8});

    ret = M.uncovered(0,80);
    REQUIRE(ret.size() == 5);
    REQUIRE(ret[0] == IntT{0,0});
    REQUIRE(ret[1] == IntT{2,2});
    REQUIRE(ret[2] == IntT{4,4});
    REQUIRE(ret[3] == IntT{6,6});
    REQUIRE(ret[4] == IntT{8,80});
}

TEST_CASE("Uncovered - regression 1", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    using IntT = decltype(M)::IntervalT;

    M.update(0,3, 0);

    auto ret = M.uncovered(1,100000);
    REQUIRE(ret.size() == 1);
    REQUIRE(ret[0] == IntT{4,100000});

    ret = M.uncovered(0,3);
    REQUIRE(ret.size() == 0);
}
