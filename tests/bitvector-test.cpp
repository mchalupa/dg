#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <random>

#include "ADT/Bitvector.h"

using dg::ADT::SparseBitvector;

TEST_CASE("Querying empty set", "SparseBitvector") {
    SparseBitvector B;

   for (uint64_t i = 1; i < (1UL << 63); i *= 2) {
       REQUIRE(B.get(i) == false);
   }
}

TEST_CASE("Set few elements", "SparseBitvector") {
    SparseBitvector B;

    REQUIRE(B.get(0) == false);
    REQUIRE(B.get(1) == false);
    REQUIRE(B.get(10) == false);
    REQUIRE(B.get(1000) == false);
    REQUIRE(B.get(100000) == false);
    B.set(0);
    B.set(1);
    B.set(10);
    B.set(1000);
    B.set(100000);
    REQUIRE(B.get(0) == true);
    REQUIRE(B.get(1) == true);
    REQUIRE(B.get(10) == true);
    REQUIRE(B.get(1000) == true);
    REQUIRE(B.get(100000) == true);
}

TEST_CASE("Extreme values", "SparseBitvector") {
    SparseBitvector B;

    REQUIRE(B.get(0) == false);

    for (unsigned int i = 0; i < 64; ++i) {
        REQUIRE(B.get(1UL << i) == false);
        REQUIRE(B.set(1UL << i) == false);
    }

    for (unsigned int i = 0; i < 64; ++i) {
        REQUIRE(B.get(1UL << i) == true);
    }
}

TEST_CASE("Iterator and empty bitvector", "SparseBitvector") {
    SparseBitvector B;
    auto it = B.begin();
    auto et = B.end();

    REQUIRE(it == et);
    REQUIRE(B.size() == 0);
    REQUIRE(B.empty());
}

TEST_CASE("Iterator one element", "SparseBitvector") {
    SparseBitvector B;

    auto it = B.begin();
    auto et = B.end();

    REQUIRE(it == et);

    REQUIRE(B.empty());
    REQUIRE(B.set(1000000) == false);

    it = B.begin();
    et = B.end();

    REQUIRE(it != et);
    REQUIRE(*it == 1000000);
    ++it;
    REQUIRE(it == et);
}

TEST_CASE("Iterator test", "SparseBitvector") {
    SparseBitvector B;
    int n = 0;

    B.set(0);
    B.set(1);
    B.set(10);
    B.set(1000);
    B.set(100000);

    auto it = B.begin();
    auto et = B.end();

    for (; it != et; ++it) {
        size_t i = *it;
        REQUIRE((i == 0 || i == 1 || i == 10 || i == 1000 || i == 100000));
        ++n;
    }
    REQUIRE(n == 5);
}

TEST_CASE("Set continuous values", "SparseBitvector") {
    SparseBitvector B;

#define NUM 10000
    for (int i = 0; i < NUM; ++i) {
        REQUIRE(B.get(i) == false);
        B.set(i);
    }

    for (int i = 0; i < NUM; ++i) {
        REQUIRE(B.get(i) == true);
    }
#undef NUM
}

TEST_CASE("Random", "SparseBitvector") {
    SparseBitvector B;
    srand(time(nullptr));

#define NUM 10000

    std::set<size_t> numbers;
    std::default_random_engine generator;
    std::uniform_int_distribution<uint64_t> distribution(0, ~static_cast<uint64_t>(0));

SECTION("Generating random numbers and putting them to bitvector") {
    for (int i = 0; i < NUM; ++i) {
        auto x = distribution(generator);
        B.set(x);
        numbers.insert(x);
    }
}

SECTION("Checking that each generated number is in bitvector") {
    for (auto x : numbers) {
        REQUIRE(B.get(x) == true);
    }
}

SECTION("Checking that each generated number is in bitvector using iterator") {
    // try iterators
    for (auto x : B) {
        REQUIRE(numbers.count(x) > 0);
    }
}

SECTION("Checking random numbers") {
    for (int i = 0; i < NUM; ++i) {
        auto x = distribution(generator);
        if (numbers.count(x) > 0)
            REQUIRE(B.get(x) == true);
    }
}
}

TEST_CASE("Regression 1", "SparseBitvector") {
    SparseBitvector B;
    REQUIRE(B.get(~static_cast<uint64_t>(0)) == false);
    REQUIRE(B.set(~static_cast<uint64_t>(0)) == false);
    REQUIRE(B.get(~static_cast<uint64_t>(0)) == true);
    auto it = B.begin();
    auto et = B.end();
    REQUIRE(it != et);
    REQUIRE(*it == ~static_cast<uint64_t>(0));
    ++it;
    REQUIRE(it == et);
}

/*
TEST_CASE("Merge bitvectors (union)", "SparseBitvector") {
    SparseBitvector B;
    REQUIRED(false);
}
*/

TEST_CASE("Merge random bitvectors (union)", "SparseBitvector") {
    SparseBitvector B1;
    SparseBitvector B2;


    std::default_random_engine generator;
    std::uniform_int_distribution<uint64_t> distribution(0, ~static_cast<uint64_t>(0));

#undef NUM
#define NUM 100
    for (int i = 0; i < NUM; ++i) {
        auto x = distribution(generator);
        auto y = distribution(generator);
        B1.set(x);
        B2.set(y);
    }

    auto B1_old = B1;
    B1.merge(B2);
    for (auto x : B1_old) {
        REQUIRE(B1.get(x));
    }
    for (auto x : B2) {
        REQUIRE(B1.get(x));
    }

//    B2.merge(B1);
//    REQUIRE(B1 == B2);
}
