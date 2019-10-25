#include <assert.h>
#include <cstdarg>
#include <cstdio>

#include "test-runner.h"
#include "test-dg.h"
#include "dg/legacy/DataFlowAnalysis.h"

namespace dg {
namespace tests {

class DataFlowA : public legacy::DataFlowAnalysis<TestNode>
{
public:
    DataFlowA(TestBBlock *B,
              bool (*ron)(TestNode *), uint32_t fl = 0)
        : legacy::DataFlowAnalysis<TestNode>(B, fl),
          run_on_node(ron) {}

    /* virtual */
    bool runOnNode(TestNode *n, TestNode *prev)
    {
        (void) prev;
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
        run_nums_test_interproc();
    };

    TestDG *create_circular_graph(size_t nodes_num)
    {
        assert(nodes_num > 0);
        TestDG *d = new TestDG();

        TestBBlock *B;
        TestNode **nodes = new TestNode *[nodes_num];

        for (unsigned int i = 0; i < nodes_num; ++i) {
            nodes[i] = new TestNode(i);

            d->addNode(nodes[i]);

            B = new TestBBlock(nodes[i]);
        }

        // connect to circular graph
        for (unsigned int i = 0; i < nodes_num; ++i) {
            TestBBlock *B1 = nodes[i]->getBBlock();
            TestBBlock *B2 = nodes[(i + 1) % nodes_num]->getBBlock();
            B1->addSuccessor(B2);
        }

        // graph is circular, it doesn't matter what BB will
        // be entry
        d->setEntryBB(B);
        d->setEntry(B->getFirstNode());

        // add some parameters
        DGParameters<TestNode> *params = new DGParameters<TestNode>();
        params->construct(nodes_num + 1, nodes_num + 1);
        params->construct(nodes_num + 2, nodes_num + 2);

        d->getEntry()->setParameters(params);

        return d;
    }

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
        #define NODES_NUM 10
        TestDG *d = create_circular_graph(NODES_NUM);
        // B is pointer to the last node, but since the graph is a circle,
        // it doesn't matter what BB we'll use
        DataFlowA dfa(d->getEntryBB(), no_change);
        dfa.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            check(d->getNode(i)->counter == 1,
                  "did not go through the node only one time but %d",
                  d->getNode(i)->counter);

            // zero out the counter for next dataflow run
            d->getNode(i)->counter = 0;
        }

        const auto& stats = dfa.getStatistics();
        check(stats.getBBlocksNum() == NODES_NUM, "wrong number of blocks: %d",
              stats.getBBlocksNum());
        check(stats.processedBlocks == NODES_NUM,
              "processed more blocks than %d - %d", NODES_NUM, stats.processedBlocks);
        check(stats.getIterationsNum() == 1, "did wrong number of iterations: %d",
              stats.getIterationsNum());

        DataFlowA dfa2(d->getEntryBB(), one_change);
        dfa2.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            check(d->getNode(i)->counter == 2,
                  "did not go through the node only one time but %d",
                  d->getNode(i)->counter);
        }

        const auto& stats2 = dfa2.getStatistics();
        check(stats2.getBBlocksNum() == NODES_NUM, "wrong number of blocks: %d",
              stats2.getBBlocksNum());
        check(stats2.processedBlocks == 2*NODES_NUM,
              "processed more blocks than %d - %d", 2*NODES_NUM, stats2.processedBlocks);
        check(stats2.getIterationsNum() == 2, "did wrong number of iterations: %d",
              stats2.getIterationsNum());

        #undef NODES_NUM
    }

    void run_nums_test_interproc()
    {
        #define NODES_NUM 5
        TestDG *d = create_circular_graph(NODES_NUM);

        for (auto It : *d) {
            TestDG *sub = create_circular_graph(NODES_NUM);
            It.second->addSubgraph(sub);
        }

        // B is pointer to the last node, but since the graph is a circle,
        // it doesn't matter what BB we'll use
        DataFlowA dfa(d->getEntryBB(), no_change);
        dfa.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            TestNode *n = d->getNode(i);
            check(n->counter == 1,
                  "did not go through the node only one time but %d", n->counter);

            // check that subgraphs are untouched by the dataflow
            // analysis
            for (auto sub : n->getSubgraphs()) {
                // iterate over nodes
                for (auto It : *sub) {
                    TestNode *n = It.second;
                    check(n->counter == 0,
                          "intrAproc. dataflow went to procedures (%d - %d)",
                          n->getKey(), n->counter);

                    TestBBlock *BB = n->getBBlock();
                    assert(BB);

                    check(BB->getDFSOrder() == 0, "DataFlow went into subgraph blocks");
                }
            }

            // zero out the counter for next dataflow run
            n->counter = 0;
        }

        // this did not go into the procedures, so we should have only
        // the parent graph
        const auto& stats = dfa.getStatistics();
        check(stats.getBBlocksNum() == NODES_NUM, "wrong number of blocks: %d",
              stats.getBBlocksNum());
        check(stats.processedBlocks == NODES_NUM,
              "processed different num of blocks than %d:  %d", NODES_NUM, stats.processedBlocks);
        check(stats.getIterationsNum() == 1, "did wrong number of iterations: %d",
              stats.getIterationsNum());

        DataFlowA dfa2(d->getEntryBB(), one_change,
                       legacy::DATAFLOW_INTERPROCEDURAL |
                       legacy::DATAFLOW_BB_NO_CALLSITES);
        dfa2.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            TestNode *n = d->getNode(i);
            check(n->counter == 2,
                  "did not go through the node only one time but %d", n->counter);

            // check that subgraphs are untouched by the dataflow
            // analysis
            for (auto sub : n->getSubgraphs()) {
                // iterate over nodes
                for (auto It : *sub) {
                    TestNode *n = It.second;
                    check(n->counter == 2,
                          "intErproc. dataflow did NOT went to procedures (%d - %d)",
                          n->getKey(), n->counter);

                    TestBBlock *BB = n->getBBlock();
                    assert(BB);

                    check(BB->getDFSOrder() != 0,
                         "intErproc DataFlow did NOT went into subgraph blocks");

                    n->counter = 0;
                }
            }

            // zero out the counter for next dataflow run
            n->counter = 0;
        }

        // we have NODES_NUM nodes and each node has subgraph of the
        // same size + the blocks in parent graph
        // we don't go through the parameters!
        uint64_t blocks_num = (NODES_NUM + 1) * NODES_NUM;
        const auto& stats2 = dfa2.getStatistics();
        check(stats2.getBBlocksNum() == blocks_num, "wrong number of blocks: %d",
              stats2.getBBlocksNum());
        check(stats2.processedBlocks == 2*blocks_num,
              "processed more blocks than %d - %d", 2*blocks_num, stats2.processedBlocks);
        check(stats2.getIterationsNum() == 2, "did wrong number of iterations: %d",
              stats2.getIterationsNum());

        // BBlocks now keep call-sites information, so now
        // this should work too
        DataFlowA dfa3(d->getEntryBB(), one_change,
                       legacy::DATAFLOW_INTERPROCEDURAL);
        dfa3.run();

        for (int i = 0; i < NODES_NUM; ++i) {
            TestNode *n = d->getNode(i);
            check(n->counter == 2,
                  "did not go through the node only one time but %d", n->counter);

            // check that subgraphs are untouched by the dataflow
            // analysis
            for (auto sub : n->getSubgraphs()) {
                // iterate over nodes
                for (auto It : *sub) {
                    TestNode *n = It.second;
                    check(n->counter == 2,
                          "intErproc. dataflow did NOT went to procedures (%d - %d)",
                          n->getKey(), n->counter);

                    TestBBlock *BB = n->getBBlock();
                    assert(BB);

                    check(BB->getDFSOrder() != 0,
                         "intErproc DataFlow did NOT went into subgraph blocks");

                    n->counter = 0;
                }
            }

            // zero out the counter for next dataflow run
            n->counter = 0;
        }

        // we have NODES_NUM nodes and each node has subgraph of the
        // same size + the blocks in parent graph
        const auto& stats3 = dfa3.getStatistics();
        check(stats3.getBBlocksNum() == blocks_num, "wrong number of blocks: %d",
              stats3.getBBlocksNum());
        check(stats3.processedBlocks == 2*blocks_num,
              "processed more blocks than %d - %d", 2*blocks_num, stats3.processedBlocks);
        check(stats3.getIterationsNum() == 2, "did wrong number of iterations: %d",
              stats3.getIterationsNum());

        #undef NODES_NUM
    }
};

}; // namespace tests
}; // namespace dg

int main(void)
{
    using namespace dg::tests;
    TestRunner Runner;

    Runner.add(new TestDataFlow());

    return Runner();
}
