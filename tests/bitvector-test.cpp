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

TEST_CASE("Iterator test", "SparseBitvector") {
    SparseBitvector B;
    int n = 0;
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

#define NUM 10000000
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

#define NUM 10000000

    std::vector<size_t> numbers;
    numbers.reserve(NUM);
    std::default_random_engine generator;
    std::uniform_int_distribution<uint64_t> distribution(0, ~static_cast<uint64_t>(0));

    for (int i = 0; i < NUM; ++i) {
        auto x = distribution(generator);
        REQUIRE(B.set(x) == false);
        numbers.push_back(x);
    }

    for (auto x : numbers) {
        REQUIRE(B.get(x) == true);
    }
}
