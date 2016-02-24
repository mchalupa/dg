#include <assert.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "test-runner.h"
#include "test-dg.h"

#include "analysis/PointsToFlowInsensitive.h"
#include "analysis/PointsToFlowSensitive.h"

namespace dg {
namespace tests {

using analysis::Pointer;

static void
dumpPSSNode(analysis::PSSNode *n)
{
    const char *name = n->getName();

    for (const Pointer& ptr : n->pointsTo) {
        printf("%s -> %s + %lu\n",
                name ? name : "unnamed",
                ptr.target->getName(), *ptr.offset);
    }
}

template <typename PTStoT>
class PointsToTest : public Test
{
public:
    PointsToTest(const char *n) : Test(n) {}

    void store_load()
    {
        using namespace analysis;

        PSSNode A(pss::ALLOC);
        PSSNode B(pss::ALLOC);
        PSSNode S(pss::STORE, &A, &B);
        PSSNode L(pss::LOAD, &B);

        A.addSuccessor(&B);
        B.addSuccessor(&S);
        S.addSuccessor(&L);

        PTStoT PA(&A);
        PA.run();

        check(L.doesPointsTo(&A), "L do not points to A");
    }

    void store_load2()
    {
        using namespace analysis;

        PSSNode A(pss::ALLOC);
        PSSNode B(pss::ALLOC);
        PSSNode C(pss::ALLOC);
        PSSNode S1(pss::STORE, &A, &B);
        PSSNode S2(pss::STORE, &C, &B);
        PSSNode L1(pss::LOAD, &B);
        PSSNode L2(pss::LOAD, &B);
        PSSNode L3(pss::LOAD, &B);

        A.addSuccessor(&B);
        B.addSuccessor(&C);
        C.addSuccessor(&S1);
        C.addSuccessor(&S2);
        S1.addSuccessor(&L1);
        S2.addSuccessor(&L2);
        L1.addSuccessor(&L3);
        L2.addSuccessor(&L3);

        PTStoT PA(&A);
        PA.run();

        check(L1.doesPointsTo(&A), "not L1->A");
        check(L2.doesPointsTo(&C), "not L2->C");
        check(L3.doesPointsTo(&A), "not L3->A");
        check(L3.doesPointsTo(&C), "not L3->C");

        /*
        A.setName(strdup("A"));
        B.setName(strdup("B"));
        C.setName(strdup("C"));
        L1.setName(strdup("L1"));
        L2.setName(strdup("L2"));
        L3.setName(strdup("L3"));

        dumpPSSNode(&L1);
        dumpPSSNode(&L2);
        dumpPSSNode(&L3);
        */
    }

    void store_load3()
    {
        using namespace analysis;

        PSSNode A(pss::ALLOC);
        PSSNode B(pss::ALLOC);
        PSSNode C(pss::ALLOC);
        PSSNode S1(pss::STORE, &A, &B);
        PSSNode L1(pss::LOAD, &B);
        PSSNode S2(pss::STORE, &C, &B);
        PSSNode L2(pss::LOAD, &B);

        A.addSuccessor(&B);
        B.addSuccessor(&C);
        C.addSuccessor(&S1);
        S1.addSuccessor(&L1);
        L1.addSuccessor(&S2);
        S2.addSuccessor(&L2);

        PTStoT PA(&A);
        PA.run();

        check(L1.doesPointsTo(&A), "L1 do not points to A");
        check(L2.doesPointsTo(&C), "L2 do not points to C");
    }

    void store_load4()
    {
        using namespace analysis;

        PSSNode A(pss::ALLOC);
        A.setSize(8);
        PSSNode B(pss::ALLOC);
        PSSNode C(pss::ALLOC);
        PSSNode GEP(pss::GEP, &A, 4);
        PSSNode S1(pss::STORE, &GEP, &B);
        PSSNode L1(pss::LOAD, &B);
        PSSNode S2(pss::STORE, &C, &B);
        PSSNode L2(pss::LOAD, &B);

        A.addSuccessor(&B);
        B.addSuccessor(&C);
        C.addSuccessor(&GEP);
        GEP.addSuccessor(&S1);
        S1.addSuccessor(&L1);
        L1.addSuccessor(&S2);
        S2.addSuccessor(&L2);

        PTStoT PA(&A);
        PA.run();

        check(L1.doesPointsTo(&A, 4), "L1 do not points to A[4]");
        check(L2.doesPointsTo(&C), "L2 do not points to C");

        /*
        A.setName(strdup("A"));
        B.setName(strdup("B"));
        C.setName(strdup("C"));
        L1.setName(strdup("L1 = LOAD B"));
        L2.setName(strdup("L2 = LOAD B"));
        GEP.setName(strdup("GEP A, 4"));
        S1.setName(strdup("STORE GEP to B"));
        S2.setName(strdup("STORE C to B"));

        dumpPSSNode(&L1);
        dumpPSSNode(&L2);
        */
    }

    void store_load5()
    {
        using namespace analysis;

        PSSNode A(pss::ALLOC);
        A.setSize(8);
        PSSNode B(pss::ALLOC);
        B.setSize(16);
        PSSNode C(pss::ALLOC);
        PSSNode GEP1(pss::GEP, &A, 4);
        PSSNode GEP2(pss::GEP, &B, 8);
        PSSNode S1(pss::STORE, &GEP1, &GEP2);
        PSSNode GEP3(pss::GEP, &B, 8);
        PSSNode L1(pss::LOAD, &GEP3);
        PSSNode S2(pss::STORE, &C, &B);
        PSSNode L2(pss::LOAD, &B);

        A.addSuccessor(&B);
        B.addSuccessor(&C);
        C.addSuccessor(&GEP1);
        GEP1.addSuccessor(&GEP2);
        GEP2.addSuccessor(&S1);
        S1.addSuccessor(&GEP3);
        GEP3.addSuccessor(&L1);
        L1.addSuccessor(&S2);
        S2.addSuccessor(&L2);

        PTStoT PA(&A);
        PA.run();

        check(L1.doesPointsTo(&A, 4), "L1 do not points to A[4]");
        check(L2.doesPointsTo(&C), "L2 do not points to C");

        /*
        A.setName(strdup("A"));
        B.setName(strdup("B"));
        C.setName(strdup("C"));
        L1.setName(strdup("L1 = LOAD B"));
        L2.setName(strdup("L2 = LOAD B"));

        dumpPSSNode(&L1);
        dumpPSSNode(&L2);
        */
    }

    void gep1()
    {
        using namespace analysis;

        PSSNode A(pss::ALLOC);
        // we must set size, so that GEP won't
        // make the offset UNKNOWN
        A.setSize(8);
        PSSNode B(pss::ALLOC);
        PSSNode GEP1(pss::GEP, &A, 4);
        PSSNode S(pss::STORE, &B, &GEP1);
        PSSNode GEP2(pss::GEP, &A, 4);
        PSSNode L(pss::LOAD, &GEP2);

        A.addSuccessor(&B);
        B.addSuccessor(&GEP1);
        GEP1.addSuccessor(&S);
        S.addSuccessor(&GEP2);
        GEP2.addSuccessor(&L);

        PTStoT PA(&A);
        PA.run();

        check(GEP1.doesPointsTo(&A, 4), "not GEP1 -> A + 4");
        check(GEP2.doesPointsTo(&A, 4), "not GEP2 -> A + 4");
        check(L.doesPointsTo(&B), "not L -> B + 0");

        /*
        A.setName(strdup("ALLOC A"));
        B.setName(strdup("ALLOC B"));
        L.setName(strdup("L = LOAD GEP2"));
        S.setName(strdup("S = STORE B to GEP1"));
        GEP1.setName(strdup("GEP1 = GEP A, 4"));
        GEP2.setName(strdup("GEP2 = GEP A, 4"));

        dumpPSSNode(&GEP1);
        dumpPSSNode(&GEP2);
        dumpPSSNode(&L);
        */
    }

    void gep2()
    {
        using namespace analysis;

        PSSNode A(pss::ALLOC);
        A.setSize(16);
        PSSNode B(pss::ALLOC);
        PSSNode GEP1(pss::GEP, &A, 4);
        PSSNode GEP2(pss::GEP, &GEP1, 4);
        PSSNode S(pss::STORE, &B, &GEP2);
        PSSNode GEP3(pss::GEP, &A, 8);
        PSSNode L(pss::LOAD, &GEP3);

        A.addSuccessor(&B);
        B.addSuccessor(&GEP1);
        GEP1.addSuccessor(&GEP2);
        GEP2.addSuccessor(&S);
        S.addSuccessor(&GEP3);
        GEP3.addSuccessor(&L);

        PTStoT PA(&A);
        PA.run();

        check(GEP1.doesPointsTo(&A, 4), "not GEP1 -> A + 4");
        //check(GEP2.doesPointsTo(&A, 8), "not GEP2 -> A + 4");
        //check(L.doesPointsTo(&B), "not L -> B + 0");
        check(L.doesPointsTo(&B), "not L -> B + 0");

        /*
        A.setName(strdup("A"));
        B.setName(strdup("B"));
        L.setName(strdup("L"));
        GEP1.setName(strdup("GEP1"));
        GEP2.setName(strdup("GEP2"));
        GEP3.setName(strdup("GEP3"));

        dumpPSSNode(&GEP1);
        dumpPSSNode(&GEP2);
        dumpPSSNode(&GEP3);
        dumpPSSNode(&L);
        */
    }

    void gep3()
    {
        using namespace analysis;

        PSSNode A(pss::ALLOC);
        PSSNode B(pss::ALLOC);
        PSSNode ARRAY(pss::ALLOC);
        ARRAY.setSize(40);
        PSSNode GEP1(pss::GEP, &ARRAY, 0);
        PSSNode GEP2(pss::GEP, &ARRAY, 4);
        PSSNode S1(pss::STORE, &A, &GEP1);
        PSSNode S2(pss::STORE, &B, &GEP2);
        PSSNode GEP3(pss::GEP, &ARRAY, 0);
        PSSNode GEP4(pss::GEP, &ARRAY, 4);
        PSSNode L1(pss::LOAD, &GEP3);
        PSSNode L2(pss::LOAD, &GEP4);

        A.addSuccessor(&B);
        B.addSuccessor(&ARRAY);
        ARRAY.addSuccessor(&GEP1);
        GEP1.addSuccessor(&GEP2);
        GEP2.addSuccessor(&S1);
        S1.addSuccessor(&S2);
        S2.addSuccessor(&GEP3);
        GEP3.addSuccessor(&GEP4);
        GEP4.addSuccessor(&L1);
        L1.addSuccessor(&L2);

        PTStoT PA(&A);
        PA.run();

        check(L1.doesPointsTo(&A), "not L1->A");
        check(L2.doesPointsTo(&B), "not L2->B");
    }

    void gep4()
    {
        using namespace analysis;

        PSSNode A(pss::ALLOC);
        PSSNode B(pss::ALLOC);
        PSSNode ARRAY(pss::ALLOC);
        ARRAY.setSize(40);
        PSSNode GEP1(pss::GEP, &ARRAY, 0);
        PSSNode GEP2(pss::GEP, &ARRAY, 4);
        PSSNode S1(pss::STORE, &A, &GEP1);
        PSSNode S2(pss::STORE, &B, &GEP2);
        PSSNode GEP3(pss::GEP, &ARRAY, 0);
        PSSNode GEP4(pss::GEP, &ARRAY, 4);
        PSSNode L1(pss::LOAD, &GEP3);
        PSSNode L2(pss::LOAD, &GEP4);

        A.addSuccessor(&B);
        B.addSuccessor(&ARRAY);
        ARRAY.addSuccessor(&GEP1);
        ARRAY.addSuccessor(&GEP2);

        GEP1.addSuccessor(&S1);
        S1.addSuccessor(&GEP3);

        GEP2.addSuccessor(&S2);
        S2.addSuccessor(&GEP3);

        GEP3.addSuccessor(&GEP4);
        GEP4.addSuccessor(&L1);
        L1.addSuccessor(&L2);

        PTStoT PA(&A);
        PA.run();

        check(L1.doesPointsTo(&A), "not L1->A");
        check(L2.doesPointsTo(&B), "not L2->B");
    }

    void gep5()
    {
        using namespace analysis;

        PSSNode A(pss::ALLOC);
        PSSNode B(pss::ALLOC);
        PSSNode ARRAY(pss::ALLOC);
        ARRAY.setSize(20);
        PSSNode GEP1(pss::GEP, &ARRAY, 0);
        PSSNode GEP2(pss::GEP, &ARRAY, 4);
        PSSNode S1(pss::STORE, &A, &GEP1);
        PSSNode S2(pss::STORE, &B, &GEP2);
        PSSNode GEP3(pss::GEP, &ARRAY, 0);
        PSSNode GEP4(pss::GEP, &ARRAY, 4);
        PSSNode L1(pss::LOAD, &GEP3);
        PSSNode L2(pss::LOAD, &GEP4);
        PSSNode GEP5(pss::GEP, &ARRAY, 0);
        PSSNode S3(pss::STORE, &B, &GEP5);
        PSSNode L3(pss::LOAD, &GEP5);

        A.addSuccessor(&B);
        B.addSuccessor(&ARRAY);
        ARRAY.addSuccessor(&GEP1);
        ARRAY.addSuccessor(&GEP2);

        GEP1.addSuccessor(&S1);
        S1.addSuccessor(&GEP3);

        GEP2.addSuccessor(&S2);
        S2.addSuccessor(&GEP5);
        GEP5.addSuccessor(&S3);
        S3.addSuccessor(&L3);
        L3.addSuccessor(&GEP3);

        GEP3.addSuccessor(&GEP4);
        GEP4.addSuccessor(&L1);
        L1.addSuccessor(&L2);

        PTStoT PA(&A);
        PA.run();

        check(L1.doesPointsTo(&A), "not L1->A");
        check(L1.doesPointsTo(&B), "not L1->B");
        check(L2.doesPointsTo(&B), "not L2->B");
        check(L3.doesPointsTo(&B), "not L2->B");
    }

    void nulltest()
    {
        using namespace analysis;

        PSSNode B(pss::ALLOC);
        PSSNode S(pss::STORE, NULLPTR, &B);
        PSSNode L(pss::LOAD, &B);

        B.addSuccessor(&S);
        S.addSuccessor(&L);

        PTStoT PA(&B);
        PA.run();

        B.setName(strdup("B"));
        L.setName(strdup("L"));

        dumpPSSNode(&L);

        check(L.doesPointsTo(NULLPTR), "L do not points to NULL");
    }

    void constant_store()
    {
        using namespace analysis;


        PSSNode A(pss::ALLOC);
        PSSNode B(pss::ALLOC);
        B.setSize(16);
        PSSNode C(pss::CONSTANT, analysis::Pointer(&B, 4));
        PSSNode S(pss::STORE, &A, &C);
        PSSNode GEP(pss::GEP, &B, 4);
        PSSNode L(pss::LOAD, &GEP);

        A.addSuccessor(&B);
        B.addSuccessor(&S);
        S.addSuccessor(&GEP);
        GEP.addSuccessor(&L);

        PTStoT PA(&A);
        PA.run();

        A.setName(strdup("A"));
        B.setName(strdup("B"));
        L.setName(strdup("L"));
        C.setName(strdup("C (const)"));

        dumpPSSNode(&L);
        dumpPSSNode(&C);

        check(L.doesPointsTo(&A), "L do not points to A");
    }



    void test()
    {
        store_load();
        store_load2();
        store_load3();
        store_load4();
        store_load5();
        gep1();
        gep2();
        gep3();
        gep4();
        gep5();
        nulltest();
        constant_store();
    }
};

class FlowInsensitivePointsToTest
    : public PointsToTest<analysis::PointsToFlowInsensitive>
{
public:
    FlowInsensitivePointsToTest()
        : PointsToTest<analysis::PointsToFlowInsensitive>
          ("flow-insensitive points-to test") {}
};

class FlowSensitivePointsToTest
    : public PointsToTest<analysis::PointsToFlowSensitive>
{
public:
    FlowSensitivePointsToTest()
        : PointsToTest<analysis::PointsToFlowSensitive>
          ("flow-sensitive points-to test") {}
};

}; // namespace tests
}; // namespace dg

int main(int argc, char *argv[])
{
    using namespace dg::tests;
    TestRunner Runner;

    Runner.add(new FlowInsensitivePointsToTest());
    Runner.add(new FlowSensitivePointsToTest());

    return Runner();
}
