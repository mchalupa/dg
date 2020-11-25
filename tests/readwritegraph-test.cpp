#include "catch.hpp"

#include "dg/ReadWriteGraph/ReadWriteGraph.h"

using namespace dg::dda;

TEST_CASE("empty ctor", "[RWBBlock]") {
    RWBBlock block;
}

TEST_CASE("subgraph ctor", "[RWBBlock]") {
    RWSubgraph subg;
    RWBBlock B(&subg);
    REQUIRE(B.getSubgraph() == &subg);
}

TEST_CASE("split around singleton", "[RWBBlock]") {
    RWNode A;
    RWBBlock B;
    B.append(&A);

    auto blks = B.splitAround(&A);
    CHECK(blks.first == nullptr);
    CHECK(blks.second == nullptr);
}

TEST_CASE("split around no prefix", "[RWBBlock]") {
    RWNode A, B;
    RWBBlock block;
    RWBBlock succ;
    RWBBlock succssucc;
    block.addSuccessor(&succ);
    block.append(&A);
    block.append(&B);

    auto blks = block.splitAround(&A);
    REQUIRE(blks.first == nullptr);
    REQUIRE(blks.second != nullptr);

    CHECK(blks.second->size() == 1);
    CHECK(*blks.second->getNodes().begin() == &B);

    CHECK(block.getSingleSuccessor() == blks.second.get());
    CHECK(blks.second->getSingleSuccessor() == &succ);
}

TEST_CASE("split around no suffix", "[RWBBlock]") {
    RWNode A, B;
    RWBBlock block;
    RWBBlock succ;
    block.addSuccessor(&succ);
    block.append(&A);
    block.append(&B);

    auto blks = block.splitAround(&B);
    REQUIRE(blks.first != nullptr);
    CHECK(blks.second == nullptr);

    CHECK(blks.first->size() == 1);
    CHECK(*blks.first->getNodes().begin() == &B);
    CHECK(block.size() == 1);
    CHECK(*block.getNodes().begin() == &A);

    CHECK(block.getSingleSuccessor() == blks.first.get());
    CHECK(blks.first->getSingleSuccessor() == &succ);
}

TEST_CASE("split around in middle", "[RWBBlock]") {
    RWNode A, B, C;
    RWBBlock block;
    RWBBlock succ;
    block.addSuccessor(&succ);
    block.append(&A);
    block.append(&B);
    block.append(&C);

    auto blks = block.splitAround(&B);
    REQUIRE(blks.first != nullptr);
    REQUIRE(blks.second != nullptr);

    CHECK(block.size() == 1);
    CHECK(*block.getNodes().begin() == &A);

    CHECK(blks.first->size() == 1);
    CHECK(*blks.first->getNodes().begin() == &B);

    CHECK(blks.second->size() == 1);
    CHECK(*blks.second->getNodes().begin() == &C);

    CHECK(block.getSingleSuccessor() == blks.first.get());
    CHECK(blks.first->getSingleSuccessor() == blks.second.get());
    CHECK(blks.second->getSingleSuccessor() == &succ);
}

