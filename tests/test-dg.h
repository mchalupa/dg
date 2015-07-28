#ifndef _TEST_DG_H_
#define _TEST_DG_H_

#include "../src/DependenceGraph.h"

namespace dg {
namespace tests {

class TestDG;

class TestNode : public Node<TestDG, int, TestNode>
{
public:
    TestNode(int k)
        : Node<TestDG, int, TestNode>(k),
          counter(0) {}

    int counter;
};

class TestDG : public DependenceGraph<int, TestNode>
{
public:
#ifdef ENABLE_CFG
    typedef BBlock<TestNode> BasicBlock;
#endif // ENABLE_CFG

    bool addNode(TestNode *n)
    {
        return DependenceGraph<int, TestNode>
                ::addNode(n->getKey(), n);
    }
};


} // namespace tests
} // namespace dg

#endif // _TEST_DG_H_
