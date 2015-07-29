#include <assert.h>
#include <cstdarg>
#include <cstdio>

#include "llvm/LLVMDependenceGraph.h"
#include "analysis/DFS.h"
#include "test-runner.h"


namespace dg {
namespace tests {

struct TestConstructors : public Test
{
    TestConstructors() : Test("constructor tests") {}

    void test()
    {
        LLVMDependenceGraph *d = new LLVMDependenceGraph();
        // we leak it, but if we would call destructor,
        // it would raise SIGABRT because we do not have
        // set entry basic block
    }
};

struct TestRefcount : public Test
{
    TestRefcount() : Test("reference counting test") {}

    void test()
    {
        LLVMDependenceGraph d;
        LLVMDependenceGraph s;

        int rc;
        rc = s.ref();
        check(rc == 2, "refcount shold be 2, but is %d", rc);
        rc = s.unref();
        check(rc == 1, "refcount shold be 1, but is %d", rc);

        s.ref();
        rc = s.ref();
        check(rc == 3, "refcount shold be 3, but is %d", rc);
        s.unref();
        rc = s.unref();
        check(rc == 1, "refcount shold be 1, but is %d", rc);

        // addSubgraph increases refcount
        LLVMNode n1(nullptr), n2(nullptr);
        n1.addSubgraph(&s);
        n2.addSubgraph(&s);

        // we do not have a getter for refcounts, so just
        // inc and dec the counter to get the current value
        s.ref();
        rc = s.unref();
        check(rc == 3, "refcount shold be 3, but is %d", rc);

        // set entry blocks, otherwise the constructor will call assert
        LLVMBBlock *entryBB1 = new LLVMBBlock(&n1);
        LLVMBBlock *entryBB2 = new LLVMBBlock(&n2);

        d.setEntryBB(entryBB1);
        s.setEntryBB(entryBB2);
    }
};

}
}

int main(int argc, char *argv[])
{
    using namespace dg::tests;

    TestRunner Runner;

    Runner.add(new TestConstructors());
    Runner.add(new TestRefcount());

    return Runner();
}
