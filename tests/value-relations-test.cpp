#include "catch.hpp"
#include "dg/llvm/ValueRelations/RelationsGraph.h"
#include <iostream>
#include <sstream>

using namespace dg::vr;

using CollectedEdges = std::vector<Bucket::RelationEdge>;

std::string dump(const CollectedEdges &edges) {
    std::ostringstream out;
    out << "{ ";
    for (auto &item : edges)
        out << item << ", ";
    out << "}";
    return out.str();
}

CollectedEdges collect(RelationsGraph::iterator begin,
                       RelationsGraph::iterator end) {
    CollectedEdges result;

    std::copy(begin, end, std::back_inserter(result));
    return result;
}

TEST_CASE("edge iterator") {
    RelationsGraph graph;

    SECTION("no nodes") { REQUIRE(graph.begin() == graph.end()); }

    Bucket &one = graph.getNewBucket();

    SECTION("one node") { REQUIRE(graph.begin() == graph.end()); }

    Bucket &two = graph.getNewBucket();

    SECTION("two nodes") {
        REQUIRE(graph.begin() == graph.end());

        size_t relIndex = GENERATE(range(0ul, allRelations.size()));
        DYNAMIC_SECTION("setting " << toRelation(relIndex)) {
            graph.addRelation(one, toRelation(relIndex), two);

            CollectedEdges result =
                    collect(graph.begin(allRelations), graph.end());
            if (toRelation(relIndex) == RelationType::EQ) {
                CHECK(result.size() == 0);
            } else {
                INFO(dump(result));
                INFO(graph);
                CHECK(result.size() == 2);
            }
        }
    }
}

TEST_CASE("testing relations") {
    RelationsGraph graph;

    Bucket &one = graph.getNewBucket();
    Bucket &two = graph.getNewBucket();

    SECTION("unrelated") {
        size_t relIndex = GENERATE(range(0ul, allRelations.size()));
        DYNAMIC_SECTION(toRelation(relIndex)) {
            REQUIRE(!graph.areRelated(one, toRelation(relIndex), two));
            REQUIRE(!graph.areRelated(two, toRelation(relIndex), one));
        }
    }

    SECTION("reflexive") {
        REQUIRE(graph.areRelated(one, RelationType::EQ, one));
        REQUIRE(graph.areRelated(two, RelationType::EQ, two));
    }

    size_t relIndex = GENERATE(range(0ul, allRelations.size()));
    DYNAMIC_SECTION("set and test " << toRelation(relIndex)) {
        graph.addRelation(one, toRelation(relIndex), two);
        INFO(one);
        if (toRelation(relIndex) == RelationType::EQ)
            CHECK(graph.areRelated(one, toRelation(relIndex), one));
        else {
            INFO(two);
            std::cerr << toRelation(relIndex) << std::endl;
            CHECK(graph.areRelated(one, toRelation(relIndex), two));
        }
    }

    SECTION("transitive") {
        Bucket &three = graph.getNewBucket();

        RelationType fst = GENERATE(RelationType::LT, RelationType::LE);
        RelationType snd = GENERATE(RelationType::LT, RelationType::LE);

        graph.addRelation(one, fst, two);
        graph.addRelation(two, snd, three);

        RelationType x = fst == RelationType::LT || snd == RelationType::LT
                                 ? RelationType::LT
                                 : RelationType::LE;
        CHECK(graph.areRelated(one, x, three));
    }
}