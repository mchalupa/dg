#include <assert.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "test-runner.h"
#include "test-dg.h"

#include "dg/analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/analysis/ReachingDefinitions/RDMap.h"

namespace dg {
namespace tests {

using namespace analysis::rd;

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

class ReachingDefinitionsTest : public Test
{
public:
    ReachingDefinitionsTest()
        : Test("Reaching definitions test") {}

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

        ReachingDefinitionsAnalysis RD(&AL1);
        RD.run();

        // get reaching definitions of 0-th byte
        // (mem, off, len)
        auto rd = RD.getReachingDefinitions(&U1);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U2);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U3);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U4);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        // offset 4 should not be defined, since we had
        // defined 0 - 3 offsets (we're starting from 0)

        rd = RD.getReachingDefinitions(&U5);
        check(rd.size() == 0, "Should have had no r.d.");
    }

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

        ReachingDefinitionsAnalysis RD(&AL1);
        RD.run();

        auto rd = RD.getReachingDefinitions(&U1);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U2);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U3);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U4);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        // offset 4 should not be defined, since we had
        // defined 0 - 3 offsets (we're starting from 0)
        rd = RD.getReachingDefinitions(&U5);
        check(rd.size() == 0, "Should have had no r.d.");
    }

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

        ReachingDefinitionsAnalysis RD(&AL1);
        RD.run();

        auto rd = RD.getReachingDefinitions(&U1);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");

        rd = RD.getReachingDefinitions(&U2);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");

        rd = RD.getReachingDefinitions(&U3);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");

        rd = RD.getReachingDefinitions(&U4);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");

        rd = RD.getReachingDefinitions(&U5);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U6);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U7);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U8);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U9);
        check(rd.size() == 0, "Should not have r.d.");
    }

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

        ReachingDefinitionsAnalysis RD(&AL1);
        RD.run();

        // bytes 0 and 1 should be defined on S1
        auto rd = RD.getReachingDefinitions(&U1);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");

        rd = RD.getReachingDefinitions(&U2);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");

        // bytes 2 and 3 should be defined on both S1 and S2
        rd = RD.getReachingDefinitions(&U3);
        check(rd.size() == 2, "Should have two r.d.");

        rd = RD.getReachingDefinitions(&U4);
        check(rd.size() == 2, "Should have two r.d.");

        rd = RD.getReachingDefinitions(&U5);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U6);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd = RD.getReachingDefinitions(&U7);
        check(rd.size() == 0, "Should not have r.d.");
    }

    void test()
    {
        basic1();
        basic2();
        basic3();
        basic4();
    }
};

}; // namespace tests
}; // namespace dg

int main(void)
{
    using namespace dg::tests;
    TestRunner Runner;

    Runner.add(new ReachingDefinitionsTest());

    return Runner();
}
