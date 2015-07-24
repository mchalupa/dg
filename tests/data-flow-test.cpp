#include <assert.h>
#include <cstdarg>
#include <cstdio>

#include "test-runner.h"
#include "test-dg.h"
#include "../src/analysis/DataFlowAnalysis.h"

namespace dg {
namespace tests {

class DataFlowA : public analysis::DataFlowAnalysis<TestNode *>
{
public:
    DataFlowA(TestDG::BasicBlock *B,
              bool (*ron)(TestNode *))
        : analysis::DataFlowAnalysis<TestNode *>(B),
          run_on_node(ron) {}

    /* virtual */
    bool runOnNode(TestNode *n)
    {
        return run_on_node(n);
    }
private:
    bool (*run_on_node)(TestNode *);
};

// put it somewhere else
class TestDataFlow : public Test
{
public:
    TestDataFlow() : Test("data flow analysis test")
    {}

    void test()
    {
        run_nums_test();
    };

    // if for each node we return that nothing
    // changed, we should go through every node
    // just once
    static bool no_change(TestNode *n)
    {
        ++n->counter;
        return false;
    }

    // change first time, no change second time
    static bool one_change(TestNode *n)
    {
        ++n->counter;
        if (n->counter == 1)
            return true;
        else
            return false;
    }

    void run_nums_test()
    {
        TestDG d; // XXX

        #define NODES_NUM 10
        TestDG::BasicBlock *B;
        TestNode **nodes = new TestNode *[NODES_NUM];

        for (int i = 0; i < NODES_NUM; ++i) {
            nodes[i] = new TestNode(i);

            // XXX
            d.addNode(nodes[i]);

            B = new TestDG::BasicBlock(nodes[i], nodes[i]);
        }

        // create circular graph
        for (int i = 0; i < NODES_NUM; ++i) {
            TestDG::BasicBlock *B1 = nodes[i]->getBasicBlock();
            TestDG::BasicBlock *B2 = nodes[(i + 1) % NODES_NUM]->getBasicBlock();
            B1->addSuccessor(B2);
        }

        // B is pointer to the last node, but since the graph is a circle,
        // it doesn't matter what BB we'll use
        DataFlowA dfa(B, no_change);
        dfa.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            check(nodes[i]->counter == 1,
                  "did not go through the node only one time but %d",
                  nodes[i]->counter);

            // zero out the counter for next dataflow run
            nodes[i]->counter = 0;
        }

        DataFlowA dfa2(B, one_change);
        dfa2.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            check(nodes[i]->counter == 2,
                  "did not go through the node only one time but %d",
                  nodes[i]->counter);
        }
    }
};

}; // namespace tests
}; // namespace dg

int main(int argc, char *argv[])
{
    using namespace dg::tests;
    TestRunner Runner;

    Runner.add(new TestDataFlow());

    return Runner();
}
