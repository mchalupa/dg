#include "catch.hpp"
#include "dg/llvm/ValueRelations/RelationsGraph.h"
#include <iostream>
#include <sstream>

using namespace dg::vr;

using CollectedEdges = std::vector<Bucket::RelationEdge>;
using RelationsMap = RelationsGraph::RelationsMap;

std::string dump(const CollectedEdges &edges) {
    std::ostringstream out;
    out << "{ ";
    for (auto &item : edges)
        out << item << ", ";
    out << "}";
    return out.str();
}

std::string dump(const RelationsMap &map) {
    std::ostringstream out;
    out << "{ ";
    for (auto &pair : map) {
        out << "{ " << pair.first.get().id << ": " << pair.second << " }, ";
    }
    out << "}" << std::endl;
    return out.str();
}

// std::ostream& operator<<(std::ostream& out, const RelationsMap::value_type&
// pair) {
//    out << pair.first << ": " << pair.second;
//    return out;
//}

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

void reportSet(RelationsGraph &graph, const Bucket &one, Relations::Type rel,
               const Bucket &two) {
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

void checkRelations(const RelationsGraph &graph, const Bucket &start,
                    size_t expectedSize) {
    INFO("explored from " << start.id);
    CollectedEdges result = collect(graph.begin_related(start, allRelations),
                                    graph.end_related(start));
    checkSize(result, graph, expectedSize);
}

void checkRelations(const RelationsMap &real, const RelationsMap &expected) {
    INFO(dump(real));
    INFO(dump(expected));
    for (auto &pair : expected) {
        auto found = real.find(pair.first);
        if (found == real.end()) {
            INFO("no relations found for " << pair.first.get().id);
            CHECK(false);
            return;
        } else {
            INFO("relations to " << pair.first.get().id);
            CHECK(found->second == pair.second);
        }
    }
    CHECK(real.size() == expected.size());
}

void checkRelations(const RelationsGraph &graph, const Bucket &start,
                    const RelationsMap &expected) {
    INFO("relations from " << start.id);
    RelationsMap real = graph.getRelated(start, allRelations);
    checkRelations(real, expected);
}

bool forbids(Relations::Type one, Relations::Type two) {
    return Relations::conflicting(one).has(two);
}

bool inferrs(Relations::Type one, Relations::Type two) {
    switch (one) {
    case Relations::LE:
        return two == Relations::NE || two == Relations::LT;
    case Relations::GE:
        return two == Relations::NE || two == Relations::GT;
    case Relations::NE:
        return two == Relations::GE || two == Relations::LE;
    case Relations::EQ:
    case Relations::LT:
    case Relations::GT:
    case Relations::PT:
    case Relations::PF:
        return false;
    }
    assert(0 && "unreachable");
    abort();
}

#define GEN_NONEQ_REL()                                                        \
    GENERATE(from_range(std::next(Relations::all.begin()),                     \
                        Relations::all.end()))
#define GEN_REL() GENERATE(from_range(Relations::all))

TEST_CASE("edge iterator") {
    RelationsGraph graph;

    SECTION("no nodes") { REQUIRE(graph.begin() == graph.end()); }

    const Bucket &one = graph.getNewBucket();

    SECTION("one node") { REQUIRE(graph.begin() == graph.end()); }

    const Bucket &two = graph.getNewBucket();

    SECTION("two nodes path") {
        Relations::Type relation = GEN_NONEQ_REL();

        DYNAMIC_SECTION("setting " << relation) {
            reportSet(graph, one, relation, two);

            checkEdges(graph, 1);
        }
    }

    SECTION("two nodes cycle") {
        Relations::Type relOne = GEN_NONEQ_REL();
        Relations::Type relTwo = GEN_NONEQ_REL();

        if (!forbids(relOne, relTwo)) {
            DYNAMIC_SECTION("setting " << relOne << " " << relTwo) {
                reportSet(graph, one, relOne, two);
                Relations before = graph.getRelated(one, allRelations)[two];
                reportSet(graph, one, relTwo, two);

                if ((relOne == Relations::LE && relTwo == Relations::GE) ||
                    (relOne == Relations::GE && relTwo == Relations::LE))
                    checkEdges(graph, 0);
                else if (before.has(relTwo) || inferrs(relOne, relTwo))
                    checkEdges(graph, 1);
                else
                    checkEdges(graph, 2);
            }
        }
    }

    const Bucket &three = graph.getNewBucket();

    SECTION("three node one relation cycle") {
        Relations::Type relOne = GEN_NONEQ_REL();

        if (relOne != Relations::LT && relOne != Relations::GT) {
            DYNAMIC_SECTION("setting " << relOne) {
                reportSet(graph, three, relOne, one);
                reportSet(graph, one, relOne, two);
                reportSet(graph, two, relOne, three);

                if (relOne == Relations::LE || relOne == Relations::GE)
                    checkEdges(graph, 0);
                else
                    checkEdges(graph, 3);
            }
        }
    }

    SECTION("three node dag") {
        Relations::Type relOne = GEN_NONEQ_REL();
        Relations::Type relTwo = GEN_NONEQ_REL();
        DYNAMIC_SECTION("setting " << relOne << " " << relTwo) {
            SECTION("chain") {
                reportSet(graph, one, relOne, two);
                reportSet(graph, two, relTwo, three);
                if (relOne == Relations::PF && relTwo == Relations::PT)
                    checkEdges(graph, 1);
                else
                    checkEdges(graph, 2);
            }

            SECTION("fork 2 - 1 - 3") {
                reportSet(graph, one, relOne, two);
                reportSet(graph, one, relTwo, three);
                if ((relOne == Relations::PT && relTwo == Relations::PT))
                    checkEdges(graph, 1);
                else
                    checkEdges(graph, 2);
            }

            SECTION("other fork 3 - 2 - 1") {
                reportSet(graph, three, relOne, two);
                reportSet(graph, one, relTwo, two);
                if ((relOne == Relations::PF && relTwo == Relations::PF))
                    checkEdges(graph, 1);
                else
                    checkEdges(graph, 2);
            }
        }
    }

    SECTION("three node different relations cycle") {
        Relations::Type relOne = GEN_NONEQ_REL();
        Relations::Type relTwo = GEN_NONEQ_REL();
        Relations::Type relThree = GEN_NONEQ_REL();
        auto ptpf = [](Relations::Type one, Relations::Type two) {
            return one == Relations::PT && two == Relations::PF;
        };
        DYNAMIC_SECTION("setting " << relOne << " " << relTwo) {
            reportSet(graph, one, relOne, two);
            reportSet(graph, two, relTwo, three);
            Relations between = graph.getRelated(one, allRelations)[three];

            if (ptpf(relTwo, relOne)) // equals one and three
                checkEdges(graph, 1);
            else if ((ptpf(relThree, relTwo) &&
                      forbids(relOne, Relations::EQ)) ||
                     (ptpf(relOne, relThree) &&
                      forbids(relTwo, Relations::EQ)) ||
                     between.conflictsWith(Relations::inverted(relThree)))
                checkEdges(graph, 2);
            else {
                DYNAMIC_SECTION("and " << relThree) {
                    reportSet(graph, three, relThree, one);

                    if ((relOne == relTwo && relTwo == relThree &&
                         (relOne == Relations::LE || relOne == Relations::GE)))
                        checkEdges(graph, 0);
                    else if (ptpf(relThree, relTwo) || ptpf(relOne, relThree))
                        checkEdges(graph, 1);
                    else if (between.has(Relations::inverted(relThree)))
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

    const Bucket &one = graph.getNewBucket();
    const Bucket &two = graph.getNewBucket();

    SECTION("unrelated") {
        Relations::Type rel = GEN_REL();
        DYNAMIC_SECTION(rel) {
            REQUIRE(!graph.areRelated(one, rel, two));
            REQUIRE(!graph.areRelated(two, rel, one));
        }
    }

    SECTION("reflexive") {
        REQUIRE(graph.areRelated(one, Relations::EQ, one));
        REQUIRE(graph.areRelated(two, Relations::EQ, two));
    }

    SECTION("set and test") {
        Relations::Type rel = GEN_REL();
        DYNAMIC_SECTION(rel) {
            graph.addRelation(one, rel, two);
            INFO(graph);
            if (rel == Relations::EQ)
                CHECK(graph.areRelated(one, rel, one));
            else {
                CHECK(graph.areRelated(one, rel, two));
            }
        }
    }

    SECTION("transitive") {
        const Bucket &three = graph.getNewBucket();

        Relations::Type fst = GENERATE(Relations::LT, Relations::LE);
        Relations::Type snd = GENERATE(Relations::LT, Relations::LE);

        graph.addRelation(one, fst, two);
        graph.addRelation(two, snd, three);

        Relations::Type x = fst == Relations::LT || snd == Relations::LT
                                    ? Relations::LT
                                    : Relations::LE;
        CHECK(graph.areRelated(one, x, three));
    }
}

TEST_CASE("big graph") {
    RelationsGraph graph;

    const Bucket &one = graph.getNewBucket();
    const Bucket &two = graph.getNewBucket();
    const Bucket &three = graph.getNewBucket();
    const Bucket &four = graph.getNewBucket();
    const Bucket &five = graph.getNewBucket();
    const Bucket &six = graph.getNewBucket();
    const Bucket &seven = graph.getNewBucket();

    Relations eq = Relations().eq().le().ge();
    Relations le = Relations().le();
    Relations lt = Relations().lt().le().ne();
    Relations ge = Relations().ge();
    Relations gt = Relations().gt().ge().ne();
    Relations pt = Relations().pt();
    Relations pf = Relations().pf();

    SECTION("LE cycle") {
        graph.addRelation(two, Relations::GE, one);
        checkEdges(graph, 1);
        graph.addRelation(two, Relations::LE, three);
        checkEdges(graph, 2);
        graph.addRelation(three, Relations::LE, four);
        checkEdges(graph, 3);
        graph.addRelation(four, Relations::LE, five);
        checkEdges(graph, 4);
        graph.addRelation(six, Relations::GE, five);
        checkEdges(graph, 5);
        graph.addRelation(seven, Relations::GE, six);
        checkEdges(graph, 6);
        graph.addRelation(seven, Relations::LE, one);
        checkEdges(graph, 0);
    }

    SECTION("mess") {
        graph.addRelation(two, Relations::GE, three);
        checkEdges(graph, 1);
        graph.addRelation(five, Relations::LE, three);
        checkEdges(graph, 2);
        graph.addRelation(six, Relations::PF, four);
        checkEdges(graph, 3);
        graph.addRelation(three, Relations::GE, six);
        checkEdges(graph, 4);
        graph.addRelation(five, Relations::LT, six);
        checkEdges(graph, 5);
        graph.addRelation(four, Relations::PT, seven);
        checkEdges(graph, 5);
        graph.addRelation(two, Relations::LE, three);
        checkEdges(graph, 4);

        SECTION("relations") {
            checkRelations(graph, one, 0);
            checkRelations(graph, two, 3);
            // three was deleted
            checkRelations(graph, four, 1);
            checkRelations(graph, five, 3);
            checkRelations(graph, six, 3);
            // seven was deleted

            checkRelations(graph, one, {{one, eq}});
            checkRelations(graph, two, {{two, eq}, {five, gt}, {six, ge}});
            checkRelations(graph, four, {{four, eq}, {six, pt}});
            checkRelations(graph, five, {{five, eq}, {two, lt}, {six, lt}});
            checkRelations(graph, six,
                           {{six, eq}, {two, le}, {four, pf}, {five, gt}});
        }
    }

    SECTION("cascade load eq") {
        reportSet(graph, one, Relations::PT, three);
        checkEdges(graph, 1);
        reportSet(graph, two, Relations::PT, four);
        checkEdges(graph, 2);
        reportSet(graph, three, Relations::PT, five);
        checkEdges(graph, 3);
        reportSet(graph, four, Relations::PT, six);
        checkEdges(graph, 4);

        reportSet(graph, seven, Relations::PT, one);
        checkEdges(graph, 5);

        reportSet(graph, seven, Relations::PT, two);
        checkEdges(graph, 3);

        SECTION("relations") {
            checkRelations(graph, seven, 1);
            checkRelations(graph, one, 2);
            // two was deleted
            checkRelations(graph, three, 2);
            // four was deleted
            checkRelations(graph, five, 1);
            // six was deleted
        }
    }

    SECTION("to first strict") {
        reportSet(graph, one, Relations::GT, three);
        checkEdges(graph, 1);
        reportSet(graph, one, Relations::GE, four);
        checkEdges(graph, 2);
        reportSet(graph, one, Relations::GT, five);
        checkEdges(graph, 3);
        reportSet(graph, two, Relations::GT, five);
        checkEdges(graph, 4);
        reportSet(graph, three, Relations::GE, six);
        checkEdges(graph, 5);
        reportSet(graph, four, Relations::GT, six);
        checkEdges(graph, 6);
        reportSet(graph, five, Relations::GT, seven);
        checkEdges(graph, 7);

        SECTION("relations") {
            checkRelations(graph, one, 6);
            checkRelations(graph, two, 2);
            checkRelations(graph, three, 2);
            checkRelations(graph, four, 2);
            checkRelations(graph, five, 3);
            checkRelations(graph, six, 4);
            checkRelations(graph, seven, 3);

            checkRelations(graph, one,
                           {{one, eq},
                            {three, gt},
                            {four, ge},
                            {five, gt},
                            {six, gt},
                            {seven, gt}});
            checkRelations(graph, two, {{two, eq}, {five, gt}, {seven, gt}});
            checkRelations(graph, three, {{one, lt}, {three, eq}, {six, ge}});
            checkRelations(graph, four, {{one, le}, {four, eq}, {six, gt}});
            checkRelations(graph, five,
                           {{one, lt}, {two, lt}, {five, eq}, {seven, gt}});
            checkRelations(graph, six,
                           {{one, lt}, {three, le}, {four, lt}, {six, eq}});
            checkRelations(graph, seven,
                           {{one, lt}, {two, lt}, {five, lt}, {seven, eq}});
        }

        SECTION("strict") {
            RelationsMap related = graph.getRelated(one, allRelations, true);
            checkRelations(related, {{one, eq},
                                     {three, gt},
                                     {four, ge},
                                     {five, gt},
                                     {six, gt}});
        }
    }

    SECTION("to first strict tricky") {
        reportSet(graph, one, Relations::GT, two);
        reportSet(graph, one, Relations::GE, three);
        reportSet(graph, three, Relations::GE, two);
        reportSet(graph, two, Relations::GT, four);

        RelationsMap related = graph.getRelated(one, allRelations, true);
        checkRelations(related, {{one, eq}, {two, gt}, {three, ge}});
    }
}