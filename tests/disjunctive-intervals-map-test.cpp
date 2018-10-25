#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <random>
#include <cassert>

#undef NDEBUG

#include "dg/analysis/Offset.h"
#include "dg/analysis/ReachingDefinitions/DisjunctiveIntervalMap.h"

using namespace dg::analysis::rd;
using dg::analysis::Offset;

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
