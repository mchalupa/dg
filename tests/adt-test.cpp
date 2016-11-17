#include <assert.h>
#include <cstdarg>
#include <cstdio>

#include "test-runner.h"

#include "ADT/Queue.h"

using namespace dg::ADT;

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

}; // namespace tests
}; // namespace dg

int main()
{
    using namespace dg::tests;
    TestRunner Runner;

    Runner.add(new TestLIFO());
    Runner.add(new TestFIFO());
    Runner.add(new TestPrioritySet());

    return Runner();
}
