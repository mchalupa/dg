#include <assert.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "test-runner.h"
#include "test-dg.h"

#include "analysis/ReachingDefinitions/ReachingDefinitions.h"
#include "analysis/ReachingDefinitions/RDMap.h"

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
        RDNode AL1;
        RDNode AL2;
        RDNode S1;
        RDNode S2;

        S1.addDef(&AL1, 0, 2, true /* strong update */);
        S2.addDef(&AL1, 0, 4, true /* strong update */);

        AL1.addSuccessor(&AL2);
        AL2.addSuccessor(&S1);
        S1.addSuccessor(&S2);

        ReachingDefinitionsAnalysis RD(&AL1);
        RD.run();

        std::set<RDNode *> rd;
        // get reaching definitions of 0-th byte
        // (mem, off, len)
        S2.getReachingDefinitions(&AL1, 0, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 1, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 2, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 3, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        // offset 4 should not be defined, since we had
        // defined 0 - 3 offsets (we're starting from 0)
        rd.clear();
        S2.getReachingDefinitions(&AL1, 4, 1, rd);
        check(rd.size() == 0, "Should have had no r.d.");
    }

    void basic2()
    {
        RDNode AL1;
        RDNode AL2;
        RDNode S1;
        RDNode S2;

        S1.addDef(&AL1, 0, 4, true /* strong update */);
        S2.addDef(&AL1, 0, 4, true /* strong update */);

        AL1.addSuccessor(&AL2);
        AL2.addSuccessor(&S1);
        S1.addSuccessor(&S2);

        ReachingDefinitionsAnalysis RD(&AL1);
        RD.run();

        std::set<RDNode *> rd;
        S2.getReachingDefinitions(&AL1, 0, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 1, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 2, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 3, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        // offset 4 should not be defined, since we had
        // defined 0 - 3 offsets (we're starting from 0)
        rd.clear();
        S2.getReachingDefinitions(&AL1, 4, 1, rd);
        check(rd.size() == 0, "Should have had no r.d.");
    }

    void basic3()
    {
        RDNode AL1;
        RDNode AL2;
        RDNode S1;
        RDNode S2;

        S1.addDef(&AL1, 0, 4, true /* strong update */);
        S2.addDef(&AL1, 4, 4, true /* strong update */);

        AL1.addSuccessor(&AL2);
        AL2.addSuccessor(&S1);
        S1.addSuccessor(&S2);

        ReachingDefinitionsAnalysis RD(&AL1);
        RD.run();

        std::set<RDNode *> rd;
        S2.getReachingDefinitions(&AL1, 0, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 1, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 2, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 3, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");

        rd.clear();
        S2.getReachingDefinitions(&AL1, 4, 1, rd);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 5, 1, rd);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 6, 1, rd);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 7, 1, rd);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd.clear();
        S2.getReachingDefinitions(&AL1, 8, 1, rd);
        check(rd.size() == 0, "Should not have r.d.");
    }

    void basic4()
    {
        RDNode AL1;
        RDNode AL2;
        RDNode S1;
        RDNode S2;
        /*
        AL1.setName("AL1");
        AL2.setName("AL2");
        S1.setName("S1: AL1 0+4");
        S2.setName("S2: AL1 2+4");
        */

        S1.addDef(&AL1, 0, 4, true /* strong update */);
        S2.addDef(&AL1, 2, 4, true /* strong update */);

        AL1.addSuccessor(&AL2);
        AL2.addSuccessor(&S1);
        S1.addSuccessor(&S2);

        ReachingDefinitionsAnalysis RD(&AL1);
        RD.run();

        std::set<RDNode *> rd;
        // bytes 0 and 1 should be defined on S1
        S2.getReachingDefinitions(&AL1, 0, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 1, 1, rd);
        check(rd.size() == 1, "Should have had one r.d.");
        check(*(rd.begin()) == &S1, "Should be S1");

        // bytes 2 and 3 should be defined on both S1 and S2
        rd.clear();
        S2.getReachingDefinitions(&AL1, 2, 1, rd);
        check(rd.size() == 2, "Should have two r.d.");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 3, 1, rd);
        check(rd.size() == 2, "Should have two r.d.");

        rd.clear();
        S2.getReachingDefinitions(&AL1, 4, 1, rd);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");
        rd.clear();
        S2.getReachingDefinitions(&AL1, 5, 1, rd);
        check(rd.size() == 1, "Should have had no r.d.");
        check(*(rd.begin()) == &S2, "Should be S2");

        rd.clear();
        S2.getReachingDefinitions(&AL1, 6, 1, rd);
        check(rd.size() == 0, "Should not have r.d.");

        //dumpMap(&S1);
        //dumpMap(&S2);
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
