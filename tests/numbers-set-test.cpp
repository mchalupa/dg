#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "ADT/NumberSet.h"

using namespace dg::ADT;

TEST_CASE("Querying empty set", "BitvectorNumberSet") {
    BitvectorNumberSet B;
    REQUIRE(B.empty());
    REQUIRE(B.size() == 0);
}

TEST_CASE("Add few elements", "BitvectorNumberSet") {
    BitvectorNumberSet B;
    REQUIRE(B.add(0));
    REQUIRE(B.add(1));
    REQUIRE(B.add(10));

    REQUIRE(B.size() == 3);
    REQUIRE(B.has(0));
    REQUIRE(B.has(1));
    REQUIRE(B.has(10));

    REQUIRE(!B.has(3));
    REQUIRE(!B.has(2));
    REQUIRE(!B.has(100));
}

TEST_CASE("Add big elements", "BitvectorNumberSet") {
    BitvectorNumberSet B;
    REQUIRE(B.add(100));
    REQUIRE(B.add(100000));
    REQUIRE(B.add(100000000));

    REQUIRE(B.size() == 3);
    REQUIRE(B.has(100));
    REQUIRE(B.has(100000));
    REQUIRE(B.has(100000000));

    REQUIRE(!B.has(3));
    REQUIRE(!B.has(2));
    REQUIRE(!B.has(1000));
}

TEST_CASE("Querying empty small set", "SmallNumberSet") {
    SmallNumberSet B;
    REQUIRE(B.empty());
    REQUIRE(B.size() == 0);
}

TEST_CASE("Add few elements (small-set)", "SmallNumberSet") {
    SmallNumberSet B;

    REQUIRE(B.add(0));
    REQUIRE(B.add(1));
    REQUIRE(B.add(10));

    REQUIRE(B.size() == 3);
    REQUIRE(B.has(0));
    REQUIRE(B.has(1));
    REQUIRE(B.has(10));

    REQUIRE(!B.has(3));
    REQUIRE(!B.has(2));
    REQUIRE(!B.has(100));
}

TEST_CASE("Add big elements (small-set)", "SmallNumberSet") {
    SmallNumberSet B;
    REQUIRE(B.add(100));
    REQUIRE(B.add(100000));
    REQUIRE(B.add(100000000));

    REQUIRE(B.size() == 3);
    REQUIRE(B.has(100));
    REQUIRE(B.has(100000));
    REQUIRE(B.has(100000000));

    REQUIRE(!B.has(3));
    REQUIRE(!B.has(2));
    REQUIRE(!B.has(1000));
}

TEST_CASE("Iterate few elements", "SmallNumberSet") {
    BitvectorNumberSet B;
    std::set<uint64_t> S;

    REQUIRE(B.add(0));
    REQUIRE(B.add(1));
    REQUIRE(B.add(10));
    S.insert({0,1,10});

    REQUIRE(B.size() == 3);
    for (auto x : B) {
        REQUIRE(S.count(x) > 0);
    }
}

TEST_CASE("Iterate big elements", "SmallNumberSet") {
    BitvectorNumberSet B;
    std::set<uint64_t> S;

    S.insert({100,100000,1000000000000000});
    for (auto x : S)
        REQUIRE(B.add(x));

    REQUIRE(B.size() == 3);
    for (auto x : B) {
        REQUIRE(S.count(x) > 0);
    }
}

TEST_CASE("Iterate mixed elements", "SmallNumberSet") {
    BitvectorNumberSet B;
    std::set<uint64_t> S;

    S.insert({0, 1, 10, 63, 64, 100,100000,1000000000000000});
    for (auto x : S)
        REQUIRE(B.add(x));

    REQUIRE(B.size() == S.size());
    for (auto x : B) {
        REQUIRE(S.count(x) > 0);
    }

    for (auto x : S)
        REQUIRE(B.has(x));
}

