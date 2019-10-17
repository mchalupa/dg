#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/analysis/ReachingDefinitions/RDMap.h"

using namespace dg::analysis;

/*
#ifdef DEBUG_ENABLED
static void
dumpMap(RDNode *node)
{
    RDMap& map = node->getReachingDefinitions();
    for (auto it : map) {
        const char *tname = it.first.target->getName();
        printf("%s %lu - %lu @ ",
               tname ? tname : "<noname>",
               *it.first.offset, *it.first.offset + *it.first.len);
        for (RDNode *site : it.second) {
            const char *sname = site->getName();
            printf("%s\n", sname ? sname : "<noname>");
        }
    }
    printf("---\n");
}
#endif
*/

template <typename RDType>
void basic1()
{
    RDNode AL1, AL2;
    RDNode S1, S2;
    RDNode U1, U2, U3, U4, U5;

    S1.addDef(&AL1, 0, 2, true /* strong update */);
    S2.addDef(&AL1, 0, 4, true /* strong update */);
    U1.addUse(&AL1, 0, 1);
    U2.addUse(&AL1, 1, 1);
    U3.addUse(&AL1, 2, 1);
    U4.addUse(&AL1, 3, 1);
    U5.addUse(&AL1, 4, 1);

    AL1.addSuccessor(&AL2);
    AL2.addSuccessor(&S1);
    S1.addSuccessor(&S2);
    S2.addSuccessor(&U1);
    U1.addSuccessor(&U2);
    U2.addSuccessor(&U3);
    U3.addSuccessor(&U4);

    RDType RD(&AL1);
    RD.run();

    // get reaching definitions of 0-th byte
    // (mem, off, len)
    auto rd = RD.getReachingDefinitions(&U1);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U2);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U3);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U4);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    // offset 4 should not be defined, since we had
    // defined 0 - 3 offsets (we're starting from 0)
    rd = RD.getReachingDefinitions(&U5);
    CHECK(rd.size() == 0);
}

template <typename RDType>
void basic2()
{
    RDNode AL1, AL2;
    RDNode S1, S2;
    RDNode U1, U2, U3, U4, U5;

    S1.addDef(&AL1, 0, 4, true /* strong update */);
    S2.addDef(&AL1, 0, 4, true /* strong update */);

    U1.addUse(&AL1, 0, 1);
    U2.addUse(&AL1, 1, 1);
    U3.addUse(&AL1, 2, 1);
    U4.addUse(&AL1, 3, 1);
    U5.addUse(&AL1, 4, 1);

    AL1.addSuccessor(&AL2);
    AL2.addSuccessor(&S1);
    S1.addSuccessor(&S2);
    S2.addSuccessor(&U1);
    U1.addSuccessor(&U2);
    U2.addSuccessor(&U3);
    U3.addSuccessor(&U4);

    RDType RD(&AL1);
    RD.run();

    auto rd = RD.getReachingDefinitions(&U1);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U2);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U3);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U4);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    // offset 4 should not be defined);
    // defined 0 - 3 offsets (we're starting from 0)
    rd = RD.getReachingDefinitions(&U5);
    CHECK(rd.size() == 0);
}

template <typename RDType>
void basic3()
{
    RDNode AL1, AL2;
    RDNode S1, S2;
    RDNode U1, U2, U3, U4, U5, U6, U7, U8, U9;

    S1.addDef(&AL1, 0, 4, true /* strong update */);
    S2.addDef(&AL1, 4, 4, true /* strong update */);

    U1.addUse(&AL1, 0, 1);
    U2.addUse(&AL1, 1, 1);
    U3.addUse(&AL1, 2, 1);
    U4.addUse(&AL1, 3, 1);
    U5.addUse(&AL1, 4, 1);
    U6.addUse(&AL1, 5, 1);
    U7.addUse(&AL1, 6, 1);
    U8.addUse(&AL1, 7, 1);
    U9.addUse(&AL1, 8, 1);

    AL1.addSuccessor(&AL2);
    AL2.addSuccessor(&S1);
    S1.addSuccessor(&S2);
    S2.addSuccessor(&U1);
    U1.addSuccessor(&U2);
    U2.addSuccessor(&U3);
    U3.addSuccessor(&U4);
    U4.addSuccessor(&U5);
    U5.addSuccessor(&U6);
    U6.addSuccessor(&U7);
    U7.addSuccessor(&U8);
    U8.addSuccessor(&U9);

    RDType RD(&AL1);
    RD.run();

    auto rd = RD.getReachingDefinitions(&U1);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S1);

    rd = RD.getReachingDefinitions(&U2);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S1);

    rd = RD.getReachingDefinitions(&U3);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S1);

    rd = RD.getReachingDefinitions(&U4);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S1);

    rd = RD.getReachingDefinitions(&U5);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U6);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U7);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U8);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U9);
    CHECK(rd.size() == 0);
}

template <typename RDType>
void basic4()
{
    RDNode AL1, AL2;
    RDNode S1, S2;
    RDNode U1, U2, U3, U4, U5, U6, U7, U8, U9;

    S1.addDef(&AL1, 0, 4, true /* strong update */);
    S2.addDef(&AL1, 2, 4, true /* strong update */);

    U1.addUse(&AL1, 0, 1);
    U2.addUse(&AL1, 1, 1);
    U3.addUse(&AL1, 2, 1);
    U4.addUse(&AL1, 3, 1);
    U5.addUse(&AL1, 4, 1);
    U6.addUse(&AL1, 5, 1);
    U7.addUse(&AL1, 6, 1);

    AL1.addSuccessor(&AL2);
    AL2.addSuccessor(&S1);
    S1.addSuccessor(&S2);
    S2.addSuccessor(&U1);
    U1.addSuccessor(&U2);
    U2.addSuccessor(&U3);
    U3.addSuccessor(&U4);
    U4.addSuccessor(&U5);
    U5.addSuccessor(&U6);
    U6.addSuccessor(&U7);
    U7.addSuccessor(&U8);
    U8.addSuccessor(&U9);

    RDType RD(&AL1);
    RD.run();

    // bytes 0 and 1 should be defined on S1
    auto rd = RD.getReachingDefinitions(&U1);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S1);

    rd = RD.getReachingDefinitions(&U2);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S1);

    // bytes 2 and 3 should be defined on both S1 and S2
    rd = RD.getReachingDefinitions(&U3);
    CHECK(rd.size() == 2);

    rd = RD.getReachingDefinitions(&U4);
    CHECK(rd.size() == 2);

    rd = RD.getReachingDefinitions(&U5);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U6);
    CHECK(rd.size() == 1);
    CHECK(*(rd.begin()) == &S2);

    rd = RD.getReachingDefinitions(&U7);
    CHECK(rd.size() == 0);
}

TEST_CASE("Basic1 data-flow", "[data-flow]") {
    basic1<ReachingDefinitionsAnalysis>();
}

TEST_CASE("Basic2 data-flow", "[data-flow]") {
    basic2<ReachingDefinitionsAnalysis>();
}

TEST_CASE("Basic3 data-flow", "[data-flow]") {
    basic3<ReachingDefinitionsAnalysis>();
}

TEST_CASE("Basic4 data-flow", "[data-flow]") {
    basic4<ReachingDefinitionsAnalysis>();
}

/*
TEST_CASE("Basic1 memory-ssa", "[memory-ssa]") {
    basic1<SSAReachingDefinitionsAnalysis>();
}

TEST_CASE("Basic2 memory-ssa", "[memory-ssa]") {
    basic2<SSAReachingDefinitionsAnalysis>();
}

TEST_CASE("Basic3 memory-ssa", "[memory-ssa]") {
    basic3<SSAReachingDefinitionsAnalysis>();
}

TEST_CASE("Basic4 memory-ssa", "[memory-ssa]") {
    basic4<SSAReachingDefinitionsAnalysis>();
}
*/
