#include "catch.hpp"
#include "dg/llvm/ValueRelations/RelationsGraph.h"

using namespace dg::vr;

TEST_CASE("main") {
    RelationsGraph graph;

    Bucket &one = graph.getNewBucket();
    Bucket &two = graph.getNewBucket();

    graph.addRelation(one, RelationType::LT, two);
    REQUIRE(graph.areRelated(one, RelationType::LT, two));
}