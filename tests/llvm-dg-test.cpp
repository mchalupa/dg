#include "catch.hpp"

#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/DFS.h"

TEST_CASE("reference counting test", "LLVM DG") {
    using namespace dg;

    LLVMDependenceGraph d;
    LLVMDependenceGraph s;

    int rc;
    rc = s.ref();
    REQUIRE(rc == 2);
    rc = s.unref();
    REQUIRE(rc == 1);

    s.ref();
    rc = s.ref();
    REQUIRE(rc == 3);
    s.unref();
    rc = s.unref();
    REQUIRE(rc == 1);

    // addSubgraph increases refcount
    LLVMNode n1(nullptr), n2(nullptr);
    n1.addSubgraph(&s);
    n2.addSubgraph(&s);

    // we do not have a getter for refcounts, so just
    // inc and dec the counter to get the current value
    s.ref();
    rc = s.unref();
    REQUIRE(rc == 3);

    // set entry blocks, otherwise the constructor will call assert
    LLVMBBlock *entryBB1 = new LLVMBBlock(&n1);
    LLVMBBlock *entryBB2 = new LLVMBBlock(&n2);

    d.setEntryBB(entryBB1);
    s.setEntryBB(entryBB2);

    delete entryBB1;
    delete entryBB2;
}

