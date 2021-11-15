#include "dg/ValueRelations/RelationsGraph.h"
#include <catch2/catch.hpp>
#include <iostream>
#include <sstream>

using namespace dg::vr;

struct Dummy {
    void areMerged(const Bucket & /*unused*/, const Bucket & /*unused*/) {}
};

using RelGraph = RelationsGraph<Dummy>;

using CollectedEdges = std::vector<Bucket::RelationEdge>;
using RelationsMap = RelGraph::RelationsMap;

std::string dump(__attribute__((unused)) const CollectedEdges &edges) {
    std::ostringstream out;
    out << "{ ";
#ifndef NDEBUG
    for (const auto &item : edges)
        out << item << ", ";
#endif
    out << "}";
    return out.str();
}

std::string dump(__attribute__((unused)) const RelationsMap &map) {
    std::ostringstream out;
    out << "{ ";
#ifndef NDEBUG
    for (const auto &pair : map) {
        out << "{ " << pair.first.get().id << ": " << pair.second << " }, ";
    }
#endif
    out << "}" << std::endl;
    return out.str();
}

CollectedEdges collect(RelGraph::iterator begin, RelGraph::iterator end) {
    CollectedEdges result;

    std::copy(begin, end, std::back_inserter(result));
    return result;
}

void checkSize(const CollectedEdges &result,
               __attribute__((unused)) const RelGraph &graph,
               size_t expectedSize) {
    INFO("result " << dump(result));
#ifndef NDEBUG
    INFO("graph:\n" << graph);
#endif
    CHECK(result.size() == expectedSize);
}

void reportSet(RelGraph &graph, const Bucket &one, Relations::Type rel,
               const Bucket &two) {
    INFO("setting " << one.id << " " << rel << " " << two.id);
    graph.addRelation(one, rel, two);
    INFO("done");
}

void checkEdges(const RelGraph &graph, size_t relationsSet) {
    SECTION("all") {
        CollectedEdges result =
                collect(graph.begin(allRelations, false), graph.end());
        checkSize(result, graph, relationsSet * 2 + graph.size());
    }
    SECTION("undirected") {
        CollectedEdges result = collect(graph.begin(), graph.end());
        checkSize(result, graph, relationsSet + graph.size());
    }
}

void checkRelations(const RelGraph &graph, const Bucket &start,
                    size_t expectedSize) {
    INFO("explored from " << start.id);
    CollectedEdges result = collect(graph.begin_related(start, allRelations),
                                    graph.end_related(start));
    checkSize(result, graph, expectedSize);
}

void checkRelations(const RelationsMap &real, const RelationsMap &expected) {
    INFO("real " << dump(real));
    INFO("expected " << dump(expected));
    for (const auto &pair : expected) {
        auto found = real.find(pair.first);
        if (found == real.end()) {
            INFO("no relations found for " << pair.first.get().id);
            CHECK(false);
        } else {
            INFO("relations to " << pair.first.get().id);
            CHECK(found->second == pair.second);
        }
    }
    CHECK(real.size() == expected.size());
}

void checkRelations(const RelGraph &graph, const Bucket &start,
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
    case Relations::SLE:
        return two == Relations::NE || two == Relations::SLT;
    case Relations::ULE:
        return two == Relations::NE || two == Relations::ULT;
    case Relations::SGE:
        return two == Relations::NE || two == Relations::SGT;
    case Relations::UGE:
        return two == Relations::NE || two == Relations::UGT;
    case Relations::NE:
        return nonStrict.has(two) || strict.has(two);
    case Relations::EQ:
    case Relations::SLT:
    case Relations::ULT:
    case Relations::SGT:
    case Relations::UGT:
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
    Dummy d;
    RelGraph graph(d);

    SECTION("no nodes") { CHECK(graph.begin() == graph.end()); }

    const Bucket &one = graph.getNewBucket();

    SECTION("one node") { checkEdges(graph, 0); }

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

                if (nonStrict.has(relOne) &&
                    relOne == Relations::inverted(relTwo))
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

        if (!strict.has(relOne)) {
            DYNAMIC_SECTION("setting " << relOne) {
                reportSet(graph, three, relOne, one);
                reportSet(graph, one, relOne, two);
                reportSet(graph, two, relOne, three);

                if (nonStrict.has(relOne))
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
            RelationsMap between = graph.getRelated(one, allRelations);

            if (ptpf(relTwo, relOne)) // equals one and three
                checkEdges(graph, 1);
            else if ((ptpf(relThree, relTwo) &&
                      forbids(relOne, Relations::EQ)) ||
                     (ptpf(relOne, relThree) &&
                      forbids(relTwo, Relations::EQ)) ||
                     between[three].conflictsWith(
                             Relations::inverted(relThree)))
                checkEdges(graph, 2);
            else {
                DYNAMIC_SECTION("and " << relThree) {
                    reportSet(graph, three, relThree, one);

                    if ((relOne == relTwo && relTwo == relThree &&
                         nonStrict.has(relOne)))
                        checkEdges(graph, 0);
                    else if (ptpf(relThree, relTwo) || ptpf(relOne, relThree))
                        checkEdges(graph, 1);
                    else if (between[three].has(Relations::inverted(relThree)))
                        checkEdges(graph, 2);
                    else
                        checkEdges(graph, 3);
                }
            }
        }
    }
}

TEST_CASE("testing relations") {
    Dummy d;
    RelGraph graph(d);

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
#ifndef NDEBUG
            INFO(graph);
#endif
            if (rel == Relations::EQ)
                CHECK(graph.areRelated(one, rel, one));
            else {
                CHECK(graph.areRelated(one, rel, two));
            }
        }
    }

    SECTION("transitive") {
        const Bucket &three = graph.getNewBucket();

        Relations::Type fst = GENERATE(Relations::SLT, Relations::SLE,
                                       Relations::ULT, Relations::ULE);
        Relations::Type snd = GENERATE(Relations::SLT, Relations::SLE,
                                       Relations::ULT, Relations::ULE);

        if (Relations::isSigned(fst) == Relations::isSigned(snd)) {
            graph.addRelation(one, fst, two);
            graph.addRelation(two, snd, three);

            Relations::Type x = nonStrict.has(fst) && nonStrict.has(snd)
                                        ? fst
                                        : (Relations::isStrict(fst)
                                                   ? fst
                                                   : Relations::getStrict(fst));
            CHECK(graph.areRelated(one, x, three));
        }
    }
}

TEST_CASE("big graph") {
    Dummy d;
    RelGraph graph(d);

    const Bucket &one = graph.getNewBucket();
    const Bucket &two = graph.getNewBucket();
    const Bucket &three = graph.getNewBucket();
    const Bucket &four = graph.getNewBucket();
    const Bucket &five = graph.getNewBucket();
    const Bucket &six = graph.getNewBucket();
    const Bucket &seven = graph.getNewBucket();

    Relations eq = Relations().eq().addImplied();
    Relations sle = Relations().sle();
    Relations slt = Relations().slt().addImplied();
    Relations ule = Relations().ule();
    Relations ult = Relations().ult().addImplied();
    Relations sge = Relations().sge();
    Relations sgt = Relations().sgt().addImplied();
    Relations uge = Relations().uge();
    Relations ugt = Relations().ugt().addImplied();
    Relations pt = Relations().pt();
    Relations pf = Relations().pf();

    SECTION("SLE cycle") {
        graph.addRelation(two, Relations::SGE, one);
        checkEdges(graph, 1);
        graph.addRelation(two, Relations::SLE, three);
        checkEdges(graph, 2);
        graph.addRelation(three, Relations::SLE, four);
        checkEdges(graph, 3);
        graph.addRelation(four, Relations::SLE, five);
        checkEdges(graph, 4);
        graph.addRelation(six, Relations::SGE, five);
        checkEdges(graph, 5);
        graph.addRelation(seven, Relations::SGE, six);
        checkEdges(graph, 6);
        graph.addRelation(seven, Relations::SLE, one);
        checkEdges(graph, 0);
    }

    SECTION("ULE cycle") {
        graph.addRelation(two, Relations::UGE, one);
        checkEdges(graph, 1);
        graph.addRelation(two, Relations::ULE, three);
        checkEdges(graph, 2);
        graph.addRelation(three, Relations::ULE, four);
        checkEdges(graph, 3);
        graph.addRelation(four, Relations::ULE, five);
        checkEdges(graph, 4);
        graph.addRelation(six, Relations::UGE, five);
        checkEdges(graph, 5);
        graph.addRelation(seven, Relations::UGE, six);
        checkEdges(graph, 6);
        graph.addRelation(seven, Relations::ULE, one);
        checkEdges(graph, 0);
    }

    SECTION("mess") {
        graph.addRelation(two, Relations::SGE, three);
        checkEdges(graph, 1);
        graph.addRelation(five, Relations::SLE, three);
        checkEdges(graph, 2);
        graph.addRelation(six, Relations::PF, four);
        checkEdges(graph, 3);
        graph.addRelation(three, Relations::SGE, six);
        checkEdges(graph, 4);
        graph.addRelation(five, Relations::SLT, six);
        checkEdges(graph, 5);
        graph.addRelation(four, Relations::PT, seven);
        checkEdges(graph, 5);
        graph.addRelation(five, Relations::UGT, two);
        checkEdges(graph, 6);
        graph.addRelation(five, Relations::UGT, three);
        checkEdges(graph, 7);
        graph.addRelation(two, Relations::SLE, three);
        checkEdges(graph, 5);

        SECTION("relations") {
            checkRelations(graph, one, 1);
            checkRelations(graph, two, 5);
            // three was deleted
            checkRelations(graph, four, 2);
            checkRelations(graph, five, 5);
            checkRelations(graph, six, 4);
            // seven was deleted

            checkRelations(graph, one, {{one, eq}});
            checkRelations(graph, two,
                           {{two, eq},
                            {five, Relations(sgt).ult().addImplied()},
                            {six, sge}});
            checkRelations(graph, four, {{four, eq}, {six, pt}});
            checkRelations(graph, five,
                           {{five, eq},
                            {two, Relations(slt).ugt().addImplied()},
                            {six, slt}});
            checkRelations(graph, six,
                           {{six, eq}, {two, sle}, {four, pf}, {five, sgt}});
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
            checkRelations(graph, seven, 2);
            checkRelations(graph, one, 3);
            // two was deleted
            checkRelations(graph, three, 3);
            // four was deleted
            checkRelations(graph, five, 2);
            // six was deleted
        }
    }

    SECTION("to first strict") {
        reportSet(graph, one, Relations::SGT, three);
        checkEdges(graph, 1);
        reportSet(graph, one, Relations::SGE, four);
        checkEdges(graph, 2);
        reportSet(graph, one, Relations::SGT, five);
        checkEdges(graph, 3);
        reportSet(graph, two, Relations::SGT, five);
        checkEdges(graph, 4);
        reportSet(graph, three, Relations::SGE, six);
        checkEdges(graph, 5);
        reportSet(graph, four, Relations::SGT, six);
        checkEdges(graph, 6);
        reportSet(graph, five, Relations::SGT, seven);
        checkEdges(graph, 7);
        reportSet(graph, seven, Relations::UGE, four);
        checkEdges(graph, 8);
        reportSet(graph, four, Relations::UGT, six);
        checkEdges(graph, 9);

        SECTION("relations") {
            checkRelations(graph, one, 7);
            checkRelations(graph, two, 3);
            checkRelations(graph, three, 3);
            checkRelations(graph, four, 5);
            checkRelations(graph, five, 4);
            checkRelations(graph, six, 7);
            checkRelations(graph, seven, 6);

            checkRelations(graph, one,
                           {{one, eq},
                            {three, sgt},
                            {four, sge},
                            {five, sgt},
                            {six, sgt},
                            {seven, sgt}});
            checkRelations(graph, two, {{two, eq}, {five, sgt}, {seven, sgt}});
            checkRelations(graph, three, {{one, slt}, {three, eq}, {six, sge}});
            checkRelations(graph, four,
                           {{one, sle},
                            {four, eq},
                            {six, Relations(sgt).ugt().addImplied()},
                            {seven, ule}});
            checkRelations(graph, five,
                           {{one, slt}, {two, slt}, {five, eq}, {seven, sgt}});
            checkRelations(graph, six,
                           {{one, slt},
                            {three, sle},
                            {four, Relations(slt).ult().addImplied()},
                            {six, eq},
                            {seven, ult}});
            checkRelations(graph, seven,
                           {{one, slt},
                            {two, slt},
                            {four, uge},
                            {five, slt},
                            {six, ugt},
                            {seven, eq}});
        }

        SECTION("strict") {
            RelationsMap related = graph.getRelated(one, allRelations, true);
            checkRelations(related, {{one, eq},
                                     {three, sgt},
                                     {four, sge},
                                     {five, sgt},
                                     {six, sgt}});
        }
    }

    SECTION("to first strict tricky") {
        reportSet(graph, one, Relations::SGT, two);
        reportSet(graph, one, Relations::SGE, three);
        reportSet(graph, three, Relations::SGE, two);
        reportSet(graph, two, Relations::SGT, four);

        RelationsMap related = graph.getRelated(one, allRelations, true);
        checkRelations(related, {{one, eq}, {two, sgt}, {three, sge}});
    }
}
