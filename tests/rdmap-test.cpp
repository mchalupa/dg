#include "catch.hpp"

#include <random>
#include <cassert>
#include <vector>

#undef NDEBUG

#include "dg/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/ReachingDefinitions/RDMap.h"

using namespace dg::analysis;

RWNode A;
RWNode B;
RWNode C;

TEST_CASE("Querying empty set", "DisjunctiveIntervalMap") {
    RDMap M;
    REQUIRE(M.empty());
}

/*
TEST_CASE("Singleton set", "DisjunctiveIntervalMap") {
    RDMap M;

    M.add(DefSite(&A, 0, 4), &B);
    REQUIRE(!M.empty());

    std::set<RWNode *> rd;
    M.get(&A, 0, 4, rd);
    REQUIRE(!rd.empty());
    REQUIRE(*(rd.begin()) == &B);

    // check full overlap
    for (int i = 0; i < 5; ++i) {
        for (int j = i; j < 5; ++j) {
            rd.clear();
            M.get(&A, i, j - i + 1, rd);
            REQUIRE(!rd.empty());
            REQUIRE(*(rd.begin()) == &B);
        }
    }

    // check partial overlap
    for (int i = 0; i < 5; ++i) {
        rd.clear();
        M.get(&A, i, 10, rd);
        REQUIRE(!rd.empty());
        REQUIRE(*(rd.begin()) == &B);
    }
}

TEST_CASE("2-elem set", "DisjunctiveIntervalMap") {
    RDMap M;

    M.add(DefSite(&A, 0, 2), &B);
    M.add(DefSite(&A, 3, 4), &B);
    REQUIRE(!M.empty());

    std::set<RWNode *> rd;
    M.get(&A, 0, 4, rd);
    REQUIRE(!rd.empty());
    REQUIRE(*(rd.begin()) == &B);

    // check full overlap
    for (int i = 0; i < 5; ++i) {
        for (int j = i; j < 5; ++j) {
            rd.clear();
            M.get(&A, i, j - i + 1, rd);
            REQUIRE(!rd.empty());
            REQUIRE(*(rd.begin()) == &B);
        }
    }

    // check partial overlap
    for (int i = 0; i < 5; ++i) {
        rd.clear();
        M.get(&A, i, 10, rd);
        REQUIRE(!rd.empty());
        REQUIRE(*(rd.begin()) == &B);
    }
}

TEST_CASE("iterator", "DisjunctiveIntervalMap") {
    RDMap M;

    REQUIRE(M.begin() == M.begin());
    REQUIRE(M.end() == M.end());
    REQUIRE(M.begin() == M.end());

    M.add(DefSite(&A, 0, 4), &B);
    REQUIRE(!M.empty());
    REQUIRE(M.begin() == M.begin());
    REQUIRE(M.begin() != M.end());
    REQUIRE(M.end() == M.end());

    auto it = M.begin();
    REQUIRE(it == it);
    REQUIRE(it == M.begin());
    REQUIRE(it != M.end());

    auto d = *it;
    REQUIRE(d.first.target == &A);
    REQUIRE(d.first.offset == 0);
    REQUIRE(d.first.len == 5);
    REQUIRE(d.second.size() == 1);
    REQUIRE(*(d.second.begin()) == &B);

    ++it;
    REQUIRE(it == M.end());
    REQUIRE(it != M.begin());
}
*/


