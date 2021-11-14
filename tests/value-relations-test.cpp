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

void checkSize(const CollectedEdges &result, const RelationsGraph &graph,
               size_t expectedSize) {
    INFO("result " << dump(result));
    INFO("graph:\n" << graph);
    CHECK(result.size() == expectedSize);
}

void reportSet(RelationsGraph &graph, Bucket &one, RelationType rel,
               Bucket &two) {
    INFO("setting " << one.id << " " << rel << " " << two.id);
    graph.addRelation(one, rel, two);
    INFO("done");
}

void checkEdges(const RelationsGraph &graph, size_t relationsSet) {
    SECTION("all") {
        CollectedEdges result = collect(graph.begin(allRelations), graph.end());
        checkSize(result, graph, relationsSet * 2);
    }
    SECTION("undirected") {
        CollectedEdges result = collect(graph.begin(), graph.end());
        checkSize(result, graph, relationsSet);
    }
}

RelationType generateNonEQRelation() {
    return toRelation(GENERATE(range(1ul, allRelations.size())));
}

RelationType generateRelation() {
    return toRelation(GENERATE(range(0ul, allRelations.size())));
}

TEST_CASE("edge iterator") {
    RelationsGraph graph;

    SECTION("no nodes") { REQUIRE(graph.begin() == graph.end()); }

    Bucket &one = graph.getNewBucket();

    SECTION("one node") { REQUIRE(graph.begin() == graph.end()); }

    Bucket &two = graph.getNewBucket();

    SECTION("two nodes") {
        REQUIRE(graph.begin() == graph.end());

        RelationType relation = generateNonEQRelation();

        DYNAMIC_SECTION("setting " << relation) {
            reportSet(graph, one, relation, two);

            checkEdges(graph, 1);
        }
    }

    Bucket &three = graph.getNewBucket();

    SECTION("three nodes") {
        REQUIRE(graph.begin() == graph.end());

        RelationType relOne = generateNonEQRelation();
        RelationType relTwo = generateNonEQRelation();

        DYNAMIC_SECTION("setting " << relOne << " " << relTwo) {
            SECTION("chain") {
                reportSet(graph, one, relOne, two);
                reportSet(graph, two, relTwo, three);
                checkEdges(graph, 2);
            }

            SECTION("fork 2 - 1 - 3") {
                reportSet(graph, one, relOne, two);
                reportSet(graph, one, relTwo, three);
                if ((relOne == RelationType::PT && relTwo == RelationType::PT))
                    checkEdges(graph, 1);
                else
                    checkEdges(graph, 2);
            }

            SECTION("other fork 3 - 2 - 1") {
                reportSet(graph, three, relOne, two);
                reportSet(graph, one, relTwo, two);
                if ((relOne == RelationType::PF && relTwo == RelationType::PF))
                    checkEdges(graph, 1);
                else
                    checkEdges(graph, 2);
            }
        }
    }
}

TEST_CASE("testing relations") {
    RelationsGraph graph;

    Bucket &one = graph.getNewBucket();
    Bucket &two = graph.getNewBucket();

    SECTION("unrelated") {
        RelationType rel = generateRelation();
        DYNAMIC_SECTION(rel) {
            REQUIRE(!graph.areRelated(one, rel, two));
            REQUIRE(!graph.areRelated(two, rel, one));
        }
    }

    SECTION("reflexive") {
        REQUIRE(graph.areRelated(one, RelationType::EQ, one));
        REQUIRE(graph.areRelated(two, RelationType::EQ, two));
    }

    RelationType rel = generateRelation();
    DYNAMIC_SECTION("set and test " << rel) {
        graph.addRelation(one, rel, two);
        INFO(graph);
        if (rel == RelationType::EQ)
            CHECK(graph.areRelated(one, rel, one));
        else {
            // INFO(two);
            CHECK(graph.areRelated(one, rel, two));
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