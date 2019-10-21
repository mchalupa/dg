#ifndef _TEST_DG_H_
#define _TEST_DG_H_

#include "dg/DependenceGraph.h"

namespace dg {

class TestDG;

class TestNode : public dg::legacy::Node<TestDG, int, TestNode>
{
public:
    TestNode(int k)
        : Node<TestDG, int, TestNode>(k),
          counter(0) {}

    int counter;
};

#ifdef ENABLE_CFG
using TestBBlock = dg::legacy::BBlock<TestNode>;
#endif // ENABLE_CFG

class TestDG : public dg::legacy::DependenceGraph<TestNode>
{
};


} // namespace dg

#endif // _TEST_DG_H_
