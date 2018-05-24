#include <assert.h>
#include <cstdarg>
#include <cstdio>

#include "test-runner.h"

#include "ADT/Queue.h"
#include "ADT/Bitvector.h"
#include "analysis/ReachingDefinitions/RDMap.h"

using namespace dg::ADT;
using dg::analysis::Offset;

namespace dg {
namespace tests {

class TestLIFO : public Test
{
public:
    TestLIFO() : Test("LIFO test")
    {}

    void test()
    {
        QueueLIFO<int> queue;
        check(queue.empty(), "empty queue not empty");

        queue.push(1);
        queue.push(13);
        queue.push(4);
        queue.push(2);
        queue.push(2);

        check(queue.pop() == 2, "Wrong pop order");
        check(queue.pop() == 2, "Wrong pop order");
        check(queue.pop() == 4, "Wrong pop order");
        check(queue.pop() == 13, "Wrong pop order");
        check(queue.pop() == 1, "Wrong pop order");
        check(queue.empty(), "emptied queue not empty");
    }
};

class TestFIFO : public Test
{
public:
    TestFIFO() : Test("FIFO test")
    {}

    void test()
    {
        QueueFIFO<int> queue;
        check(queue.empty(), "empty queue not empty");

        queue.push(1);
        queue.push(13);
        queue.push(4);
        queue.push(4);
        queue.push(2);

        check(queue.pop() == 1, "Wrong pop order");
        check(queue.pop() == 13, "Wrong pop order");
        check(queue.pop() == 4, "Wrong pop order");
        check(queue.pop() == 4, "Wrong pop order");
        check(queue.pop() == 2, "Wrong pop order");
        check(queue.empty(), "emptied queue not empty");
    }
};

struct mycomp
{
    bool operator()(int a, int b) const
    {
        return a > b;
    }
};

class TestPrioritySet : public Test
{
public:
    TestPrioritySet() : Test("test priority set")
    {}

    void test()
    {
        PrioritySet<int, mycomp> queue;
        check(queue.empty(), "empty queue not empty");

        queue.push(1);
        queue.push(13);
        queue.push(4);
        queue.push(4);
        queue.push(2);

        // we inserted twice, but it is a set
        check(queue.size() == 4, "BUG in size");

        check(queue.pop() == 13, "Wrong pop order");
        check(queue.pop() == 4, "Wrong pop order");
        check(queue.pop() == 2, "Wrong pop order");
        check(queue.pop() == 1, "Wrong pop order");
        check(queue.empty(), "emptied queue not empty");
    }
};

class TestIntervalsHandling : public Test
{
public:
    TestIntervalsHandling() : Test("test intervals handling")
    {}

    void test()
    {
        using namespace analysis::rd;

        check(intervalsDisjunctive(0, 1, 2, 20), "BUG: intervals should be disjunctive");
        check(intervalsDisjunctive(0, 1, 1, 2), "BUG: intervals should be disjunctive");
        check(!intervalsDisjunctive(1, 1, 1, 2), "BUG: intervals should not be disjunctive");
        check(!intervalsDisjunctive(1, 1, 1, 1), "BUG: intervals should not be disjunctive");
        check(!intervalsDisjunctive(3, 5, 3, 5), "BUG: intervals should not be disjunctive");
        check(!intervalsDisjunctive(3, 7, 3, 5), "BUG: intervals should not be disjunctive");
        check(!intervalsDisjunctive(3, 5, 3, 7), "BUG: intervals should not be disjunctive");
        check(intervalsDisjunctive(1, 1, 2, 2), "BUG: intervals should be disjunctive");
        check(!intervalsDisjunctive(0, 4, 2, 2), "BUG: intervals should not be disjunctive");

        check(!intervalsDisjunctive(0, 4, 2, Offset::UNKNOWN),
                                    "BUG: intervals should not be disjunctive");
        check(intervalsDisjunctive(0, 4, 4, Offset::UNKNOWN),
                                   "BUG: intervals should be disjunctive");
        check(!intervalsDisjunctive(0, Offset::UNKNOWN, 4, Offset::UNKNOWN),
                                    "BUG: intervals should not be disjunctive");
        check(!intervalsDisjunctive(0, Offset::UNKNOWN, 1, 4),
                                    "BUG: intervals should not be disjunctive");

        check(!intervalsOverlap(0, 1, 2, 20), "BUG: intervals should be disjunctive");
        check(!intervalsOverlap(0, 1, 1, 2), "BUG: intervals should not overlap");
        check(intervalsOverlap(1, 1, 1, 2), "BUG: intervals should overlap");
        check(intervalsOverlap(1, 1, 1, 1), "BUG: intervals should overlap");
        check(intervalsOverlap(3, 5, 3, 5), "BUG: intervals should overlap");
        check(intervalsOverlap(3, 7, 3, 5), "BUG: intervals should overlap");
        check(intervalsOverlap(3, 5, 3, 7), "BUG: intervals should overlap");
        check(!intervalsOverlap(1, 1, 2, 2), "BUG: intervals should be disjunctive");
        check(!intervalsOverlap(1, 2, 0, 1), "BUG: intervals should not overlap");
        check(intervalsOverlap(1, 2, 1, 1), "BUG: intervals should overlap");
        check(intervalsOverlap(1, 2, 1, 2), "BUG: intervals should overlap");
        check(intervalsOverlap(1, 2, 2, 2), "BUG: intervals should overlap");
        check(intervalsOverlap(2, 2, 2, 2), "BUG: intervals should overlap");
        check(intervalsOverlap(3, 3, 2, 2), "BUG: intervals should overlap");
        check(!intervalsOverlap(1, 2, 3, 3), "BUG: intervals should not overlap");
        check(!intervalsOverlap(1, 2, 3, 3), "BUG: intervals should not overlap");
    }
};

class TestBitvectors : public Test
{
public:
    TestBitvectors() : Test("Test bitvecotr class") {}

    void test1() {
        SparseBitvector B;
        check(!B.get(0), "Should not be set");
        check(!B.get(1), "Should not be set");
        check(!B.get(10), "Should not be set");
        check(!B.get(1000), "Should not be set");
        check(!B.get(100000), "Should not be set");
        B.set(0);
        B.set(1);
        B.set(10);
        B.set(1000);
        B.set(100000);
        check(B.get(0), "Should not be set");
        check(B.get(1), "Should not be set");
        check(B.get(10), "Should not be set");
        check(B.get(1000), "Should not be set");
        check(B.get(100000), "Should not be set");

        int n = 0;
        auto it = B.begin();
        auto et = B.end();
        for (; it != et; ++it) {
            size_t i = *it;
        //for (size_t i : B) {
            check(i == 0 || i == 1 || i == 10 || i == 1000 || i == 100000, "Wrong element in B");
            ++n;
        }
        check(n == 5, "The number of elements does not fit");
    }

    void test2() {
        SparseBitvector B;

#define NUM 13
        for (int i = 0; i < NUM; ++i) {
            check(!B.get(i), "Should not be set");
            B.set(i);
        }

        for (int i = 0; i < NUM; ++i) {
            check(B.get(i), "Should be set");
        }
    }

    void test()
    {
        test1();
        test2();
    }

};


}; // namespace tests
}; // namespace dg

int main()
{
    using namespace dg::tests;
    TestRunner Runner;

    Runner.add(new TestLIFO());
    Runner.add(new TestFIFO());
    Runner.add(new TestPrioritySet());
    Runner.add(new TestIntervalsHandling());
    Runner.add(new TestBitvectors());

    return Runner();
}
