#define CATCH_CONFIG_MAIN
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

