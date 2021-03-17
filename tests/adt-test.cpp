#include "catch.hpp"

#include "dg/ADT/Queue.h"
#include "dg/ADT/Bitvector.h"
#include "dg/ReadWriteGraph/DefSite.h"

using namespace dg::ADT;
using dg::Offset;

TEST_CASE("QuieieLIFO basic manimp", "QueueLIFO") {
    QueueLIFO<int> queue;
    REQUIRE(queue.empty());

    queue.push(1);
    queue.push(13);
    queue.push(4);
    queue.push(2);
    queue.push(2);

    REQUIRE(queue.pop() == 2);
    REQUIRE(queue.pop() == 2);
    REQUIRE(queue.pop() == 4);
    REQUIRE(queue.pop() == 13);
    REQUIRE(queue.pop() == 1);
    REQUIRE(queue.empty());
}

TEST_CASE("QueueFIFo basic manimp", "QueueFIFO") {
    QueueFIFO<int> queue;
    REQUIRE(queue.empty());

    queue.push(1);
    queue.push(13);
    queue.push(4);
    queue.push(4);
    queue.push(2);

    REQUIRE(queue.pop() == 1);
    REQUIRE(queue.pop() == 13);
    REQUIRE(queue.pop() == 4);
    REQUIRE(queue.pop() == 4);
    REQUIRE(queue.pop() == 2);
    REQUIRE(queue.empty());
}

struct mycomp {
    bool operator()(int a, int b) const { return a > b; }
};

TEST_CASE("Priority queue basic manimp", "Priority Queue") 
{
    PrioritySet<int, mycomp> queue;
    REQUIRE(queue.empty());

    queue.push(1);
    queue.push(13);
    queue.push(4);
    queue.push(4);
    queue.push(2);

    // we inserted twice, but it is a set
    REQUIRE(queue.size() == 4);

    REQUIRE(queue.pop() == 13);
    REQUIRE(queue.pop() == 4);
    REQUIRE(queue.pop() == 2);
    REQUIRE(queue.pop() == 1);
    REQUIRE(queue.empty());
}

TEST_CASE("Intervals handling", "RWG intervals") {
    using namespace dg::dda;

    REQUIRE(intervalsDisjunctive(0, 1, 2, 20));
    REQUIRE(intervalsDisjunctive(0, 1, 1, 2));
    REQUIRE(!intervalsDisjunctive(1, 1, 1, 2));
    REQUIRE(!intervalsDisjunctive(1, 1, 1, 1));
    REQUIRE(!intervalsDisjunctive(3, 5, 3, 5));
    REQUIRE(!intervalsDisjunctive(3, 7, 3, 5));
    REQUIRE(!intervalsDisjunctive(3, 5, 3, 7));
    REQUIRE(intervalsDisjunctive(1, 1, 2, 2));
    REQUIRE(!intervalsDisjunctive(0, 4, 2, 2));

    REQUIRE(!intervalsDisjunctive(0, 4, 2, Offset::UNKNOWN));
    REQUIRE(intervalsDisjunctive(0, 4, 4, Offset::UNKNOWN));
    REQUIRE(!intervalsDisjunctive(0, Offset::UNKNOWN, 4, Offset::UNKNOWN));
    REQUIRE(!intervalsDisjunctive(0, Offset::UNKNOWN, 1, 4));

    REQUIRE(!intervalsOverlap(0, 1, 2, 20));
    REQUIRE(!intervalsOverlap(0, 1, 1, 2));
    REQUIRE(intervalsOverlap(1, 1, 1, 2));
    REQUIRE(intervalsOverlap(1, 1, 1, 1));
    REQUIRE(intervalsOverlap(3, 5, 3, 5));
    REQUIRE(intervalsOverlap(3, 7, 3, 5));
    REQUIRE(intervalsOverlap(3, 5, 3, 7));
    REQUIRE(!intervalsOverlap(1, 1, 2, 2));
    REQUIRE(!intervalsOverlap(1, 2, 0, 1));
    REQUIRE(intervalsOverlap(1, 2, 1, 1));
    REQUIRE(intervalsOverlap(1, 2, 1, 2));
    REQUIRE(intervalsOverlap(1, 2, 2, 2));
    REQUIRE(intervalsOverlap(2, 2, 2, 2));
    REQUIRE(intervalsOverlap(3, 3, 2, 2));
    REQUIRE(!intervalsOverlap(1, 2, 3, 3));
    REQUIRE(!intervalsOverlap(1, 2, 3, 3));
}

#include "dg/ADT/STLHashMap.h"

template <typename MapT>
void hashMapTest() {
    MapT M;
    REQUIRE(M.get(0) == nullptr);
    auto r = M.put(1, 2);
    const int *x = M.get(1);
    REQUIRE(r);
    REQUIRE(x);
    REQUIRE(*x == 2);
    r = M.put(1, 3);
    REQUIRE(!r);
    M.put(5, 6);
    x = M.get(1);
    const int *y = M.get(5);
    REQUIRE(x);
    REQUIRE(*x == 2);
    REQUIRE(y);
    REQUIRE(*y == 6);
    M.erase(1);
    REQUIRE(M.get(1) == nullptr);
    y = M.get(5);
    REQUIRE(y);
    REQUIRE(*y == 6);
}


// create an object for testing hashing function collision
struct MyInt {
    int x;
    MyInt() : x(0) {}
    MyInt(int y) : x(y) {}
    bool operator==(const MyInt& rhs) const { return x == rhs.x; }
};

namespace std {
template <> struct hash<MyInt> {
    size_t operator()(const MyInt& mi) const {
        return mi.x % 2;
    }
};
}

template <typename MapT>
void hashCollisionTest() {
    MapT M;
    bool r;
    r = M.put(MyInt(2), 2);
    REQUIRE(r);
    r = M.put(MyInt(3), 3);
    REQUIRE(r);
    // collision
    r = M.put(MyInt(4), 4);
    REQUIRE(r);
    REQUIRE(M.size() == 3);
    for (int i = 2; i <= 4; ++i) {
        const int *x = M.get(MyInt(i));
        REQUIRE(*x == i);
    }
}

TEST_CASE("STL hashmap test", "HashMap") {
    hashMapTest<dg::STLHashMap<int, int>>();
}

TEST_CASE("STL hashmap collision test", "HashMap") {
    hashCollisionTest<dg::STLHashMap<MyInt, int>>();
}

#ifdef HAVE_TSL_HOPSCOTCH
#include "dg/ADT/TslHopscotchHashMap.h"

TEST_CASE("TSL Hopscotch hashmap test", "HashMap") {
    hashMapTest<dg::HopscotchHashMap<int, int>>();
}

TEST_CASE("TSL Hopscotch collision test", "HashMap") {
    hashCollisionTest<dg::HopscotchHashMap<MyInt, int>>();
}
#endif


