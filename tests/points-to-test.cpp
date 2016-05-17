#include <assert.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "test-runner.h"
#include "test-dg.h"

#include "analysis/PointsTo/PointsToFlowInsensitive.h"
#include "analysis/PointsTo/PointsToFlowSensitive.h"

namespace dg {
namespace tests {

using analysis::pss::Pointer;
using analysis::pss::PSNode;

static void
dumpPSNode(PSNode *n)
{
    const char *name = n->getName();
    const char *name2;

    for (const Pointer& ptr : n->pointsTo) {
        printf("%s -> ", name ? name : "unnamed");

        name2 = ptr.target->getName();
        printf("%s", name2 ? name2 : "unnamed");

        if (ptr.offset.isUnknown())
            puts(" + UNKNOWN");
        else
            printf(" + %lu\n", *ptr.offset);
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

        PSNode A(pss::ALLOC);
        PSNode B(pss::ALLOC);
        PSNode S(pss::STORE, &A, &B);
        PSNode L(pss::LOAD, &B);

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

        PSNode A(pss::ALLOC);
        PSNode B(pss::ALLOC);
        PSNode C(pss::ALLOC);
        PSNode S1(pss::STORE, &A, &B);
        PSNode S2(pss::STORE, &C, &B);
        PSNode L1(pss::LOAD, &B);
        PSNode L2(pss::LOAD, &B);
        PSNode L3(pss::LOAD, &B);

        //             A
        //             |
        //             B
        //             |
        //             C
        //           /   \
        //          S1    S2
        //          |      |
        //          L1    L2
        //            \  /
        //             L3
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

        dumpPSNode(&L1);
        dumpPSNode(&L2);
        dumpPSNode(&L3);
        */
    }

    void store_load3()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        PSNode B(pss::ALLOC);
        PSNode C(pss::ALLOC);
        PSNode S1(pss::STORE, &A, &B);
        PSNode L1(pss::LOAD, &B);
        PSNode S2(pss::STORE, &C, &B);
        PSNode L2(pss::LOAD, &B);

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

        PSNode A(pss::ALLOC);
        A.setSize(8);
        PSNode B(pss::ALLOC);
        PSNode C(pss::ALLOC);
        PSNode GEP(pss::GEP, &A, 4);
        PSNode S1(pss::STORE, &GEP, &B);
        PSNode L1(pss::LOAD, &B);
        PSNode S2(pss::STORE, &C, &B);
        PSNode L2(pss::LOAD, &B);

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

        dumpPSNode(&L1);
        dumpPSNode(&L2);
        */
    }

    void store_load5()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        A.setSize(8);
        PSNode B(pss::ALLOC);
        B.setSize(16);
        PSNode C(pss::ALLOC);
        PSNode GEP1(pss::GEP, &A, 4);
        PSNode GEP2(pss::GEP, &B, 8);
        PSNode S1(pss::STORE, &GEP1, &GEP2);
        PSNode GEP3(pss::GEP, &B, 8);
        PSNode L1(pss::LOAD, &GEP3);
        PSNode S2(pss::STORE, &C, &B);
        PSNode L2(pss::LOAD, &B);

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

        dumpPSNode(&L1);
        dumpPSNode(&L2);
        */
    }

    void gep1()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        // we must set size, so that GEP won't
        // make the offset UNKNOWN
        A.setSize(8);
        PSNode B(pss::ALLOC);
        PSNode GEP1(pss::GEP, &A, 4);
        PSNode S(pss::STORE, &B, &GEP1);
        PSNode GEP2(pss::GEP, &A, 4);
        PSNode L(pss::LOAD, &GEP2);

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

        dumpPSNode(&GEP1);
        dumpPSNode(&GEP2);
        dumpPSNode(&L);
        */
    }

    void gep2()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        A.setSize(16);
        PSNode B(pss::ALLOC);
        PSNode GEP1(pss::GEP, &A, 4);
        PSNode GEP2(pss::GEP, &GEP1, 4);
        PSNode S(pss::STORE, &B, &GEP2);
        PSNode GEP3(pss::GEP, &A, 8);
        PSNode L(pss::LOAD, &GEP3);

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

        dumpPSNode(&GEP1);
        dumpPSNode(&GEP2);
        dumpPSNode(&GEP3);
        dumpPSNode(&L);
        */
    }

    void gep3()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        PSNode B(pss::ALLOC);
        PSNode ARRAY(pss::ALLOC);
        ARRAY.setSize(40);
        PSNode GEP1(pss::GEP, &ARRAY, 0);
        PSNode GEP2(pss::GEP, &ARRAY, 4);
        PSNode S1(pss::STORE, &A, &GEP1);
        PSNode S2(pss::STORE, &B, &GEP2);
        PSNode GEP3(pss::GEP, &ARRAY, 0);
        PSNode GEP4(pss::GEP, &ARRAY, 4);
        PSNode L1(pss::LOAD, &GEP3);
        PSNode L2(pss::LOAD, &GEP4);

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

        PSNode A(pss::ALLOC);
        PSNode B(pss::ALLOC);
        PSNode ARRAY(pss::ALLOC);
        ARRAY.setSize(40);
        PSNode GEP1(pss::GEP, &ARRAY, 0);
        PSNode GEP2(pss::GEP, &ARRAY, 4);
        PSNode S1(pss::STORE, &A, &GEP1);
        PSNode S2(pss::STORE, &B, &GEP2);
        PSNode GEP3(pss::GEP, &ARRAY, 0);
        PSNode GEP4(pss::GEP, &ARRAY, 4);
        PSNode L1(pss::LOAD, &GEP3);
        PSNode L2(pss::LOAD, &GEP4);

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

        PSNode A(pss::ALLOC);
        PSNode B(pss::ALLOC);
        PSNode ARRAY(pss::ALLOC);
        ARRAY.setSize(20);
        PSNode GEP1(pss::GEP, &ARRAY, 0);
        PSNode GEP2(pss::GEP, &ARRAY, 4);
        PSNode S1(pss::STORE, &A, &GEP1);
        PSNode S2(pss::STORE, &B, &GEP2);
        PSNode GEP3(pss::GEP, &ARRAY, 0);
        PSNode GEP4(pss::GEP, &ARRAY, 4);
        PSNode L1(pss::LOAD, &GEP3);
        PSNode L2(pss::LOAD, &GEP4);
        PSNode GEP5(pss::GEP, &ARRAY, 0);
        PSNode S3(pss::STORE, &B, &GEP5);
        PSNode L3(pss::LOAD, &GEP5);

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

        PSNode B(pss::ALLOC);
        PSNode S(pss::STORE, pss::NULLPTR, &B);
        PSNode L(pss::LOAD, &B);

        B.addSuccessor(&S);
        S.addSuccessor(&L);

        PTStoT PA(&B);
        PA.run();

        check(L.doesPointsTo(pss::NULLPTR), "L do not points to NULL");
    }

    void constant_store()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        PSNode B(pss::ALLOC);
        B.setSize(16);
        PSNode C(pss::CONSTANT, &B, 4);
        PSNode S(pss::STORE, &A, &C);
        PSNode GEP(pss::GEP, &B, 4);
        PSNode L(pss::LOAD, &GEP);

        A.addSuccessor(&B);
        B.addSuccessor(&S);
        S.addSuccessor(&GEP);
        GEP.addSuccessor(&L);

        PTStoT PA(&A);
        PA.run();

        check(L.doesPointsTo(&A), "L do not points to A");
    }

    void load_from_zeroed()
    {
        using namespace analysis;

        PSNode B(pss::ALLOC);
        B.setZeroInitialized();
        PSNode L(pss::LOAD, &B);

        B.addSuccessor(&L);

        PTStoT PA(&B);
        PA.run();

        check(L.doesPointsTo(pss::NULLPTR), "L do not points to nullptr");
    }

    void load_from_unknown_offset()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        PSNode B(pss::ALLOC);
        B.setSize(20);
        PSNode GEP(pss::GEP, &B, UNKNOWN_OFFSET);
        PSNode S(pss::STORE, &A, &GEP);
        PSNode GEP2(pss::GEP, &B, 4);
        PSNode L(pss::LOAD, &GEP2); // load from B + 4

        A.addSuccessor(&B);
        B.addSuccessor(&GEP);
        GEP.addSuccessor(&S);
        S.addSuccessor(&GEP2);
        GEP2.addSuccessor(&L);

        PTStoT PA(&A);
        PA.run();

        // B points to A + 0 at unknown offset,
        // so load from B + 4 should be A + 0
        check(L.doesPointsTo(&A), "L do not points to A");
    }

    void load_from_unknown_offset2()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        PSNode B(pss::ALLOC);
        B.setSize(20);
        PSNode GEP(pss::GEP, &B, 4);
        PSNode S(pss::STORE, &A, &GEP);
        PSNode GEP2(pss::GEP, &B, UNKNOWN_OFFSET);
        PSNode L(pss::LOAD, &GEP2); // load from B + UNKNOWN_OFFSET

        A.addSuccessor(&B);
        B.addSuccessor(&GEP);
        GEP.addSuccessor(&S);
        S.addSuccessor(&GEP2);
        GEP2.addSuccessor(&L);

        PTStoT PA(&A);
        PA.run();

        // B points to A + 0 at offset 4,
        // so load from B + UNKNOWN should be A + 0
        check(L.doesPointsTo(&A), "L do not points to A");
    }

    void load_from_unknown_offset3()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        PSNode B(pss::ALLOC);
        B.setSize(20);
        PSNode GEP(pss::GEP, &B, UNKNOWN_OFFSET);
        PSNode S(pss::STORE, &A, &GEP);
        PSNode GEP2(pss::GEP, &B, UNKNOWN_OFFSET);
        PSNode L(pss::LOAD, &GEP2);

        A.addSuccessor(&B);
        B.addSuccessor(&GEP);
        GEP.addSuccessor(&S);
        S.addSuccessor(&GEP2);
        GEP2.addSuccessor(&L);

        PTStoT PA(&A);
        PA.run();

        check(L.doesPointsTo(&A), "L do not points to A");
    }

    void memcpy_test()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        A.setSize(20);
        PSNode SRC(pss::ALLOC);
        SRC.setSize(16);
        PSNode DEST(pss::ALLOC);
        DEST.setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3 and A + 12
         * at offsets 4 and 8 */
        PSNode GEP1(pss::GEP, &A, 3);
        PSNode GEP2(pss::GEP, &A, 12);
        PSNode G1(pss::GEP, &SRC, 4);
        PSNode G2(pss::GEP, &SRC, 8);
        PSNode S1(pss::STORE, &GEP1, &G1);
        PSNode S2(pss::STORE, &GEP2, &G2);

        /* copy the memory,
         * after this node dest should point to
         * A + 3 and A + 12 at offsets 4 and 8 */
        PSNode CPY(pss::MEMCPY, &SRC, &DEST,
                    0 /* from 0 */, UNKNOWN_OFFSET /* len = all */);

        /* load from the dest memory */
        PSNode G3(pss::GEP, &DEST, 4);
        PSNode G4(pss::GEP, &DEST, 8);
        PSNode L1(pss::LOAD, &G3);
        PSNode L2(pss::LOAD, &G4);

        A.addSuccessor(&SRC);
        SRC.addSuccessor(&DEST);
        DEST.addSuccessor(&GEP1);
        GEP1.addSuccessor(&GEP2);
        GEP2.addSuccessor(&G1);
        G1.addSuccessor(&G2);
        G2.addSuccessor(&S1);
        S1.addSuccessor(&S2);
        S2.addSuccessor(&CPY);
        CPY.addSuccessor(&G3);
        G3.addSuccessor(&G4);
        G4.addSuccessor(&L1);
        L1.addSuccessor(&L2);

        PTStoT PA(&A);
        PA.run();

        check(L1.doesPointsTo(&A, 3), "L do not points to A + 3");
        check(L2.doesPointsTo(&A, 12), "L do not points to A + 12");
    }

    void memcpy_test2()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        A.setSize(20);
        PSNode SRC(pss::ALLOC);
        SRC.setSize(16);
        PSNode DEST(pss::ALLOC);
        DEST.setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3 and A + 12
         * at offsets 4 and 8 */
        PSNode GEP1(pss::GEP, &A, 3);
        PSNode GEP2(pss::GEP, &A, 12);
        PSNode G1(pss::GEP, &SRC, 4);
        PSNode G2(pss::GEP, &SRC, 8);
        PSNode S1(pss::STORE, &GEP1, &G1);
        PSNode S2(pss::STORE, &GEP2, &G2);

        /* copy first 8 bytes from the memory,
         * after this node dest should point to
         * A + 3 at offset 4 (8 is 9th byte,
         * so it should not be included) */
        PSNode CPY(pss::MEMCPY, &SRC, &DEST,
                    0 /* from 0 */, 8 /* len*/);

        /* load from the dest memory */
        PSNode G3(pss::GEP, &DEST, 4);
        PSNode G4(pss::GEP, &DEST, 8);
        PSNode L1(pss::LOAD, &G3);
        PSNode L2(pss::LOAD, &G4);

        A.addSuccessor(&SRC);
        SRC.addSuccessor(&DEST);
        DEST.addSuccessor(&GEP1);
        GEP1.addSuccessor(&GEP2);
        GEP2.addSuccessor(&G1);
        G1.addSuccessor(&G2);
        G2.addSuccessor(&S1);
        S1.addSuccessor(&S2);
        S2.addSuccessor(&CPY);
        CPY.addSuccessor(&G3);
        G3.addSuccessor(&G4);
        G4.addSuccessor(&L1);
        L1.addSuccessor(&L2);

        PTStoT PA(&A);
        PA.run();

        check(L1.doesPointsTo(&A, 3), "L1 do not points to A + 3");
        check(L2.pointsTo.empty(), "L2 is not empty");
    }

    void memcpy_test3()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        A.setSize(20);
        PSNode SRC(pss::ALLOC);
        SRC.setSize(16);
        PSNode DEST(pss::ALLOC);
        DEST.setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3 and A + 12
         * at offsets 4 and 8 */
        PSNode GEP1(pss::GEP, &A, 3);
        PSNode GEP2(pss::GEP, &A, 12);
        PSNode G1(pss::GEP, &SRC, 4);
        PSNode G2(pss::GEP, &SRC, 8);
        PSNode S1(pss::STORE, &GEP1, &G1);
        PSNode S2(pss::STORE, &GEP2, &G2);

        /* copy memory from 8 bytes and further
         * after this node dest should point to
         * A + 12 at offset 8 */
        PSNode CPY(pss::MEMCPY, &SRC, &DEST,
                    8 /* from */, UNKNOWN_OFFSET /* len*/);

        /* load from the dest memory */
        PSNode G3(pss::GEP, &DEST, 4);
        PSNode G4(pss::GEP, &DEST, 8);
        PSNode L1(pss::LOAD, &G3);
        PSNode L2(pss::LOAD, &G4);

        A.addSuccessor(&SRC);
        SRC.addSuccessor(&DEST);
        DEST.addSuccessor(&GEP1);
        GEP1.addSuccessor(&GEP2);
        GEP2.addSuccessor(&G1);
        G1.addSuccessor(&G2);
        G2.addSuccessor(&S1);
        S1.addSuccessor(&S2);
        S2.addSuccessor(&CPY);
        CPY.addSuccessor(&G3);
        G3.addSuccessor(&G4);
        G4.addSuccessor(&L1);
        L1.addSuccessor(&L2);

        PTStoT PA(&A);
        PA.run();

        check(L2.doesPointsTo(&A, 12), "L2 do not points to A + 12");
        check(L1.pointsTo.empty(), "L1 is not empty");
    }

    void memcpy_test4()
    {
        using namespace analysis;

        PSNode A(pss::ALLOC);
        A.setSize(20);
        PSNode SRC(pss::ALLOC);
        SRC.setSize(16);
        SRC.setZeroInitialized();

        PSNode DEST(pss::ALLOC);
        DEST.setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3
         * offsets 4 */
        PSNode GEP1(pss::GEP, &A, 3);
        PSNode G1(pss::GEP, &SRC, 4);
        PSNode S1(pss::STORE, &GEP1, &G1);

        /* copy memory from 8 bytes and further
         * after this node dest should point to
         * A + 12 at offset 8 */
        PSNode CPY(pss::MEMCPY, &SRC, &DEST,
                    8 /* from */, UNKNOWN_OFFSET /* len*/);

        /* load from the dest memory */
        PSNode G3(pss::GEP, &SRC, 8);
        PSNode G4(pss::GEP, &DEST, 8);
        PSNode L1(pss::LOAD, &G3);
        PSNode L2(pss::LOAD, &G4);

        A.addSuccessor(&SRC);
        SRC.addSuccessor(&DEST);
        DEST.addSuccessor(&GEP1);
        GEP1.addSuccessor(&G1);
        G1.addSuccessor(&S1);
        S1.addSuccessor(&CPY);
        CPY.addSuccessor(&G3);
        G3.addSuccessor(&G4);
        G4.addSuccessor(&L1);
        L1.addSuccessor(&L2);

        PTStoT PA(&A);
        PA.run();

        check(L1.doesPointsTo(pss::NULLPTR), "L1 does not point to NULL");
        check(L2.doesPointsTo(pss::NULLPTR), "L2 does not point to NULL");
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
        load_from_zeroed();
        load_from_unknown_offset();
        load_from_unknown_offset2();
        load_from_unknown_offset3();
        memcpy_test();
        memcpy_test2();
        memcpy_test3();
        memcpy_test4();
    }
};

class FlowInsensitivePointsToTest
    : public PointsToTest<analysis::pss::PointsToFlowInsensitive>
{
public:
    FlowInsensitivePointsToTest()
        : PointsToTest<analysis::pss::PointsToFlowInsensitive>
          ("flow-insensitive points-to test") {}
};

class FlowSensitivePointsToTest
    : public PointsToTest<analysis::pss::PointsToFlowSensitive>
{
public:
    FlowSensitivePointsToTest()
        : PointsToTest<analysis::pss::PointsToFlowSensitive>
          ("flow-sensitive points-to test") {}
};

class PSNodeTest : public Test
{

public:
    PSNodeTest()
          : Test("PSNode test") {}

    void unknown_offset1()
    {
        using namespace dg::analysis::pss;
        PSNode N1(ALLOC);
        PSNode N2(LOAD, &N1);

        N2.addPointsTo(&N1, 1);
        N2.addPointsTo(&N1, 2);
        N2.addPointsTo(&N1, 3);
        check(N2.pointsTo.size() == 3);
        N2.addPointsTo(&N1, UNKNOWN_OFFSET);
        check(N2.pointsTo.size() == 1);
        check(N2.addPointsTo(&N1, 3) == false);
    }

    void test()
    {
        unknown_offset1();
    }
};

}; // namespace tests
}; // namespace dg

int main(int argc, char *argv[])
{
    using namespace dg::tests;
    TestRunner Runner;

    Runner.add(new FlowInsensitivePointsToTest());
    Runner.add(new FlowSensitivePointsToTest());
    Runner.add(new PSNodeTest());

    return Runner();
}
