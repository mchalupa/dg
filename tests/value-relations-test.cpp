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
        CollectedEdges result =
                collect(graph.begin(allRelations, false), graph.end());
        checkSize(result, graph, relationsSet * 2);
    }
    SECTION("undirected") {
        CollectedEdges result = collect(graph.begin(), graph.end());
        checkSize(result, graph, relationsSet);
    }
}

bool forbids(RelationType one, RelationType two) {
    return conflicting(one)[toInt(two)];
}

bool inferrs(RelationType one, RelationType two) {
    switch (one) {
    case RelationType::LE:
        return two == RelationType::NE || two == RelationType::LT;
    case RelationType::GE:
        return two == RelationType::NE || two == RelationType::GT;
    case RelationType::NE:
        return two == RelationType::GE || two == RelationType::LE;
    case RelationType::EQ:
    case RelationType::LT:
    case RelationType::GT:
    case RelationType::PT:
    case RelationType::PF:
        return false;
    }
    assert(0 && "unreachable");
    abort();
}

#define GEN_NONEQ_REL() toRelation(GENERATE(range(1ul, allRelations.size())))
#define GEN_REL() toRelation(GENERATE(range(0ul, allRelations.size())))

TEST_CASE("edge iterator") {
    RelationsGraph graph;

    SECTION("no nodes") { REQUIRE(graph.begin() == graph.end()); }

    Bucket &one = graph.getNewBucket();

    SECTION("one node") { REQUIRE(graph.begin() == graph.end()); }

    Bucket &two = graph.getNewBucket();

    SECTION("two nodes path") {
        RelationType relation = GEN_NONEQ_REL();

        DYNAMIC_SECTION("setting " << relation) {
            reportSet(graph, one, relation, two);

            checkEdges(graph, 1);
        }
    }

    SECTION("two nodes cycle") {
        RelationType relOne = GEN_NONEQ_REL();
        RelationType relTwo = GEN_NONEQ_REL();

        if (!forbids(relOne, relTwo)) {
            DYNAMIC_SECTION("setting " << relOne << " " << relTwo) {
                reportSet(graph, one, relOne, two);
                RelationBits before = graph.getRelated(one, allRelations)[two];
                reportSet(graph, one, relTwo, two);

                if ((relOne == RelationType::LE &&
                     relTwo == RelationType::GE) ||
                    (relOne == RelationType::GE && relTwo == RelationType::LE))
                    checkEdges(graph, 0);
                else if (before[toInt(relTwo)] || inferrs(relOne, relTwo))
                    checkEdges(graph, 1);
                else
                    checkEdges(graph, 2);
            }
        }
    }

    Bucket &three = graph.getNewBucket();

    SECTION("three node one relation cycle") {
        RelationType relOne = GEN_NONEQ_REL();

        if (relOne != RelationType::LT && relOne != RelationType::GT) {
            DYNAMIC_SECTION("setting " << relOne) {
                reportSet(graph, three, relOne, one);
                reportSet(graph, one, relOne, two);
                reportSet(graph, two, relOne, three);

                if (relOne == RelationType::LE || relOne == RelationType::GE)
                    checkEdges(graph, 0);
                else
                    checkEdges(graph, 3);
            }
        }
    }

    SECTION("three node dag") {
        RelationType relOne = GEN_NONEQ_REL();
        RelationType relTwo = GEN_NONEQ_REL();
        DYNAMIC_SECTION("setting " << relOne << " " << relTwo) {
            SECTION("chain") {
                reportSet(graph, one, relOne, two);
                reportSet(graph, two, relTwo, three);
                if (relOne == RelationType::PF && relTwo == RelationType::PT)
                    checkEdges(graph, 1);
                else
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

    SECTION("three node different relations cycle") {
        RelationType relOne = GEN_NONEQ_REL();
        RelationType relTwo = GEN_NONEQ_REL();
        RelationType relThree = GEN_NONEQ_REL();
        auto ptpf = [](RelationType one, RelationType two) {
            return one == RelationType::PT && two == RelationType::PF;
        };
        DYNAMIC_SECTION("setting " << relOne << " " << relTwo) {
            reportSet(graph, one, relOne, two);
            reportSet(graph, two, relTwo, three);
            RelationBits between = graph.getRelated(one, allRelations)[three];

            if (ptpf(relTwo, relOne)) // equals one and three
                checkEdges(graph, 1);
            else if ((ptpf(relThree, relTwo) &&
                      forbids(relOne, RelationType::EQ)) ||
                     (ptpf(relOne, relThree) &&
                      forbids(relTwo, RelationType::EQ)) ||
                     (between & conflicting(inverted(relThree))).any())
                checkEdges(graph, 2);
            else {
                DYNAMIC_SECTION("and " << relThree) {
                    reportSet(graph, three, relThree, one);

                    if ((relOne == relTwo && relTwo == relThree &&
                         (relOne == RelationType::LE ||
                          relOne == RelationType::GE)))
                        checkEdges(graph, 0);
                    else if (ptpf(relThree, relTwo) || ptpf(relOne, relThree))
                        checkEdges(graph, 1);
                    else if (between[toInt(inverted(relThree))])
                        checkEdges(graph, 2);
                    else
                        checkEdges(graph, 3);
                }
            }
        }
    }
}

TEST_CASE("testing relations") {
    RelationsGraph graph;

    Bucket &one = graph.getNewBucket();
    Bucket &two = graph.getNewBucket();

    SECTION("unrelated") {
        RelationType rel = GEN_REL();
        DYNAMIC_SECTION(rel) {
            REQUIRE(!graph.areRelated(one, rel, two));
            REQUIRE(!graph.areRelated(two, rel, one));
        }
    }

    SECTION("reflexive") {
        REQUIRE(graph.areRelated(one, RelationType::EQ, one));
        REQUIRE(graph.areRelated(two, RelationType::EQ, two));
    }

    SECTION("set and test") {
        RelationType rel = GEN_REL();
        DYNAMIC_SECTION(rel) {
            graph.addRelation(one, rel, two);
            INFO(graph);
            if (rel == RelationType::EQ)
                CHECK(graph.areRelated(one, rel, one));
            else {
                CHECK(graph.areRelated(one, rel, two));
            }
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

TEST_CASE("big graph") {
    RelationsGraph graph;

    Bucket &one = graph.getNewBucket();
    Bucket &two = graph.getNewBucket();
    Bucket &three = graph.getNewBucket();
    Bucket &four = graph.getNewBucket();
    Bucket &five = graph.getNewBucket();
    Bucket &six = graph.getNewBucket();
    Bucket &seven = graph.getNewBucket();

    SECTION("LE cycle") {
        graph.addRelation(two, RelationType::GE, one);
        checkEdges(graph, 1);
        graph.addRelation(two, RelationType::LE, three);
        checkEdges(graph, 2);
        graph.addRelation(three, RelationType::LE, four);
        checkEdges(graph, 3);
        graph.addRelation(four, RelationType::LE, five);
        checkEdges(graph, 4);
        graph.addRelation(six, RelationType::GE, five);
        checkEdges(graph, 5);
        graph.addRelation(seven, RelationType::GE, six);
        checkEdges(graph, 6);
        graph.addRelation(seven, RelationType::LE, one);
        checkEdges(graph, 0);
    }

    SECTION("mess") {
        graph.addRelation(two, RelationType::GE, three);
        checkEdges(graph, 1);
        graph.addRelation(five, RelationType::LE, three);
        checkEdges(graph, 2);
        graph.addRelation(six, RelationType::PF, four);
        checkEdges(graph, 3);
        graph.addRelation(three, RelationType::GE, six);
        checkEdges(graph, 4);
        graph.addRelation(five, RelationType::LT, six);
        checkEdges(graph, 5);
        graph.addRelation(four, RelationType::PT, seven);
        checkEdges(graph, 5);
        graph.addRelation(two, RelationType::LE, three);
        checkEdges(graph, 4);
    }
}