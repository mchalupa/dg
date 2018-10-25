#define CATCH_CONFIG_MAIN
#include "catch.hpp"


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
    M.add(0,2, 1);
    REQUIRE(M.size() == 1);
}

TEST_CASE("Add non-overlapping", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,2, 1);
    REQUIRE(M.size() == 1);
    M.add(3,4, 2);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add non-overlapping3", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(3,4, 2);
    REQUIRE(M.size() == 1);
    M.add(0,2, 1);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add non-overlapping1", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,10, 1);
    REQUIRE(M.size() == 1);
    M.add(100,101, 2);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add overlapping0", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,2, 1);
    REQUIRE(M.size() == 1);
    M.add(2,3, 2);
    REQUIRE(M.size() == 3);
}

TEST_CASE("Add overlapping0com", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(2,3, 2);
    REQUIRE(M.size() == 1);
    M.add(0,2, 1);
    REQUIRE(M.size() == 3);
}

TEST_CASE("Add overlapping", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,2, 1);
    REQUIRE(M.size() == 1);
    M.add(0,4, 2);
    REQUIRE(M.size() == 2);
}

TEST_CASE("Add overlappingCom", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,4, 2);
    REQUIRE(M.size() == 1);
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

    M.add(5,5, 3);
    REQUIRE(M.size() == 4);

    bool changed = M.add(5,5, 3);
    REQUIRE(changed == false);
    REQUIRE(M.size() == 4);

    M.add(0,10, 4);
    REQUIRE(M.size() == 7);
}

TEST_CASE("Add overlappingX", "DisjunctiveIntervalMap") {
    DisjunctiveIntervalMap<int> M;
    M.add(0,4, 1);
    M.add(1,1, 2);
    M.add(3,5, 3);
    REQUIRE(M.size() == 5);

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

