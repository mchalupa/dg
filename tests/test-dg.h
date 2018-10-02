#ifndef _TEST_DG_H_
#define _TEST_DG_H_

#include "dg/DependenceGraph.h"

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

#ifdef ENABLE_CFG
using TestBBlock = BBlock<TestNode>;
#endif // ENABLE_CFG

class TestDG : public DependenceGraph<TestNode>
{
};


} // namespace tests
} // namespace dg

#endif // _TEST_DG_H_
