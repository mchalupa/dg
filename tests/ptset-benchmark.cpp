#include <vector>
#include <string>
#include <random>

#include "analysis/PointsTo/PointsToSet.h"
#include "../tools/TimeMeasure.h"

using namespace dg::analysis::pta;

std::default_random_engine generator;
std::uniform_int_distribution<uint64_t> distribution(0, ~static_cast<uint64_t>(0));

#define run(func, msg) do { \
    std::cout << "Running " << msg << "\n"; \
    dg::debug::TimeMeasure tm; \
    tm.start(); \
    for (int i = 0; i < times; ++i) \
        func<PointsToSet>(); \
    tm.stop(); \
    tm.report(" -- PointsToSet bitvector took"); \
    tm.start(); \
    for (int i = 0; i < times; ++i) \
        func<SimplePointsToSet>(); \
    tm.stop(); \
    tm.report(" -- PointsToSet std::set took"); \
    } while(0);

template <typename PTSetT>
void test1() {
    PTSetT S;
    PSNode *x = reinterpret_cast<PSNode *>(0x1);
    PSNode *y = reinterpret_cast<PSNode *>(0x2);
    PSNode *z = reinterpret_cast<PSNode *>(0x3);

    S.add({x, 0});
    S.add({y, 0});
    S.add({z, 0});
}

template <typename PTSetT>
void test2() {
    PTSetT S;
    PSNode *x = reinterpret_cast<PSNode *>(0x1);

    S.add({x, 0});
}

template <typename PTSetT>
void test3() {
    std::set<size_t> numbers;

    PTSetT S;
    PSNode * pointers[] {
        reinterpret_cast<PSNode *>(0x1),
        reinterpret_cast<PSNode *>(0x2),
        reinterpret_cast<PSNode *>(0x3),
        reinterpret_cast<PSNode *>(0x4),
        reinterpret_cast<PSNode *>(0x5),
        reinterpret_cast<PSNode *>(0x6),
        reinterpret_cast<PSNode *>(0x7)
    };

    for (int i = 0; i < 1000; ++i) {
        auto x = distribution(generator);
        S.add(pointers[x % (sizeof(pointers)/sizeof(*pointers))], x);
    }
}

template <typename PTSetT>
void test4() {
    std::set<size_t> numbers;

    PTSetT S;
    for (int i = 0; i < 1000; ++i) {
        S.add(reinterpret_cast<PSNode *>(1), i);
    }
}

template <typename PTSetT>
void test5() {
    std::set<size_t> numbers;

    PTSetT S;
    for (int i = 0; i < 1000; ++i) {
        S.add(reinterpret_cast<PSNode *>(i), i);
    }
}


int main()
{
    int times;
    times = 100000;
    run(test1, "Adding three elements");

    times = 100000;
    run(test2, "Adding same element");

    times = 10000;
    run(test3, "Adding 1000 times 7 pointers with random offsets");

    times = 10000;
    run(test4, "Adding 1000 offsets to a pointer");

    times = 10000;
    run(test5, "Adding 1000 different pointers");
}
