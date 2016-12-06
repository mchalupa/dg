#include <assert.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "test-runner.h"
#include "test-dg.h"

#include "analysis/PointsTo/PointerSubgraph.h"
#include "analysis/PointsTo/PointsToFlowInsensitive.h"
#include "analysis/PointsTo/PointsToFlowSensitive.h"

namespace dg {
namespace tests {

using analysis::pta::Pointer;
using analysis::pta::PointerSubgraph;
using analysis::pta::PSNode;

template <typename PTStoT>
class PointsToTest : public Test
{
public:
    PointsToTest(const char *n) : Test(n) {}

    void store_load()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        PSNode B(pta::ALLOC);
        PSNode S(pta::STORE, &A, &B);
        PSNode L(pta::LOAD, &B);

        A.addSuccessor(&B);
        B.addSuccessor(&S);
        S.addSuccessor(&L);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L.doesPointsTo(&A), "L do not points to A");
    }

    void store_load2()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        PSNode B(pta::ALLOC);
        PSNode C(pta::ALLOC);
        PSNode S1(pta::STORE, &A, &B);
        PSNode S2(pta::STORE, &C, &B);
        PSNode L1(pta::LOAD, &B);
        PSNode L2(pta::LOAD, &B);
        PSNode L3(pta::LOAD, &B);

        /*
         *        A
         *        |
         *        B
         *        |
         *        C
         *      /   \
         *     S1    S2
         *     |      |
         *     L1    L2
         *       \  /
         *        L3
         */
        A.addSuccessor(&B);
        B.addSuccessor(&C);
        C.addSuccessor(&S1);
        C.addSuccessor(&S2);
        S1.addSuccessor(&L1);
        S2.addSuccessor(&L2);
        L1.addSuccessor(&L3);
        L2.addSuccessor(&L3);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L1.doesPointsTo(&A), "not L1->A");
        check(L2.doesPointsTo(&C), "not L2->C");
        check(L3.doesPointsTo(&A), "not L3->A");
        check(L3.doesPointsTo(&C), "not L3->C");
    }

    void store_load3()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        PSNode B(pta::ALLOC);
        PSNode C(pta::ALLOC);
        PSNode S1(pta::STORE, &A, &B);
        PSNode L1(pta::LOAD, &B);
        PSNode S2(pta::STORE, &C, &B);
        PSNode L2(pta::LOAD, &B);

        A.addSuccessor(&B);
        B.addSuccessor(&C);
        C.addSuccessor(&S1);
        S1.addSuccessor(&L1);
        L1.addSuccessor(&S2);
        S2.addSuccessor(&L2);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L1.doesPointsTo(&A), "L1 do not points to A");
        check(L2.doesPointsTo(&C), "L2 do not points to C");
    }

    void store_load4()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        A.setSize(8);
        PSNode B(pta::ALLOC);
        PSNode C(pta::ALLOC);
        PSNode GEP(pta::GEP, &A, 4);
        PSNode S1(pta::STORE, &GEP, &B);
        PSNode L1(pta::LOAD, &B);
        PSNode S2(pta::STORE, &C, &B);
        PSNode L2(pta::LOAD, &B);

        A.addSuccessor(&B);
        B.addSuccessor(&C);
        C.addSuccessor(&GEP);
        GEP.addSuccessor(&S1);
        S1.addSuccessor(&L1);
        L1.addSuccessor(&S2);
        S2.addSuccessor(&L2);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L1.doesPointsTo(&A, 4), "L1 do not points to A[4]");
        check(L2.doesPointsTo(&C), "L2 do not points to C");
    }

    void store_load5()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        A.setSize(8);
        PSNode B(pta::ALLOC);
        B.setSize(16);
        PSNode C(pta::ALLOC);
        PSNode GEP1(pta::GEP, &A, 4);
        PSNode GEP2(pta::GEP, &B, 8);
        PSNode S1(pta::STORE, &GEP1, &GEP2);
        PSNode GEP3(pta::GEP, &B, 8);
        PSNode L1(pta::LOAD, &GEP3);
        PSNode S2(pta::STORE, &C, &B);
        PSNode L2(pta::LOAD, &B);

        A.addSuccessor(&B);
        B.addSuccessor(&C);
        C.addSuccessor(&GEP1);
        GEP1.addSuccessor(&GEP2);
        GEP2.addSuccessor(&S1);
        S1.addSuccessor(&GEP3);
        GEP3.addSuccessor(&L1);
        L1.addSuccessor(&S2);
        S2.addSuccessor(&L2);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L1.doesPointsTo(&A, 4), "L1 do not points to A[4]");
        check(L2.doesPointsTo(&C), "L2 do not points to C");
    }

    void gep1()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        // we must set size, so that GEP won't
        // make the offset UNKNOWN
        A.setSize(8);
        PSNode B(pta::ALLOC);
        PSNode GEP1(pta::GEP, &A, 4);
        PSNode S(pta::STORE, &B, &GEP1);
        PSNode GEP2(pta::GEP, &A, 4);
        PSNode L(pta::LOAD, &GEP2);

        A.addSuccessor(&B);
        B.addSuccessor(&GEP1);
        GEP1.addSuccessor(&S);
        S.addSuccessor(&GEP2);
        GEP2.addSuccessor(&L);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(GEP1.doesPointsTo(&A, 4), "not GEP1 -> A + 4");
        check(GEP2.doesPointsTo(&A, 4), "not GEP2 -> A + 4");
        check(L.doesPointsTo(&B), "not L -> B + 0");
    }

    void gep2()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        A.setSize(16);
        PSNode B(pta::ALLOC);
        PSNode GEP1(pta::GEP, &A, 4);
        PSNode GEP2(pta::GEP, &GEP1, 4);
        PSNode S(pta::STORE, &B, &GEP2);
        PSNode GEP3(pta::GEP, &A, 8);
        PSNode L(pta::LOAD, &GEP3);

        A.addSuccessor(&B);
        B.addSuccessor(&GEP1);
        GEP1.addSuccessor(&GEP2);
        GEP2.addSuccessor(&S);
        S.addSuccessor(&GEP3);
        GEP3.addSuccessor(&L);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(GEP1.doesPointsTo(&A, 4), "not GEP1 -> A + 4");
        //check(GEP2.doesPointsTo(&A, 8), "not GEP2 -> A + 4");
        //check(L.doesPointsTo(&B), "not L -> B + 0");
        check(L.doesPointsTo(&B), "not L -> B + 0");
    }

    void gep3()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        PSNode B(pta::ALLOC);
        PSNode ARRAY(pta::ALLOC);
        ARRAY.setSize(40);
        PSNode GEP1(pta::GEP, &ARRAY, 0);
        PSNode GEP2(pta::GEP, &ARRAY, 4);
        PSNode S1(pta::STORE, &A, &GEP1);
        PSNode S2(pta::STORE, &B, &GEP2);
        PSNode GEP3(pta::GEP, &ARRAY, 0);
        PSNode GEP4(pta::GEP, &ARRAY, 4);
        PSNode L1(pta::LOAD, &GEP3);
        PSNode L2(pta::LOAD, &GEP4);

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

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L1.doesPointsTo(&A), "not L1->A");
        check(L2.doesPointsTo(&B), "not L2->B");
    }

    void gep4()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        PSNode B(pta::ALLOC);
        PSNode ARRAY(pta::ALLOC);
        ARRAY.setSize(40);
        PSNode GEP1(pta::GEP, &ARRAY, 0);
        PSNode GEP2(pta::GEP, &ARRAY, 4);
        PSNode S1(pta::STORE, &A, &GEP1);
        PSNode S2(pta::STORE, &B, &GEP2);
        PSNode GEP3(pta::GEP, &ARRAY, 0);
        PSNode GEP4(pta::GEP, &ARRAY, 4);
        PSNode L1(pta::LOAD, &GEP3);
        PSNode L2(pta::LOAD, &GEP4);

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

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L1.doesPointsTo(&A), "not L1->A");
        check(L2.doesPointsTo(&B), "not L2->B");
    }

    void gep5()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        PSNode B(pta::ALLOC);
        PSNode ARRAY(pta::ALLOC);
        ARRAY.setSize(20);
        PSNode GEP1(pta::GEP, &ARRAY, 0);
        PSNode GEP2(pta::GEP, &ARRAY, 4);
        PSNode S1(pta::STORE, &A, &GEP1);
        PSNode S2(pta::STORE, &B, &GEP2);
        PSNode GEP3(pta::GEP, &ARRAY, 0);
        PSNode GEP4(pta::GEP, &ARRAY, 4);
        PSNode L1(pta::LOAD, &GEP3);
        PSNode L2(pta::LOAD, &GEP4);
        PSNode GEP5(pta::GEP, &ARRAY, 0);
        PSNode S3(pta::STORE, &B, &GEP5);
        PSNode L3(pta::LOAD, &GEP5);

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

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L1.doesPointsTo(&A), "not L1->A");
        check(L1.doesPointsTo(&B), "not L1->B");
        check(L2.doesPointsTo(&B), "not L2->B");
        check(L3.doesPointsTo(&B), "not L2->B");
    }

    void nulltest()
    {
        using namespace analysis;

        PSNode B(pta::ALLOC);
        PSNode S(pta::STORE, pta::NULLPTR, &B);
        PSNode L(pta::LOAD, &B);

        B.addSuccessor(&S);
        S.addSuccessor(&L);

        PointerSubgraph PS(&B);
        PTStoT PA(&PS);
        PA.run();

        check(L.doesPointsTo(pta::NULLPTR), "L do not points to NULL");
    }

    void constant_store()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        PSNode B(pta::ALLOC);
        B.setSize(16);
        PSNode C(pta::CONSTANT, &B, 4);
        PSNode S(pta::STORE, &A, &C);
        PSNode GEP(pta::GEP, &B, 4);
        PSNode L(pta::LOAD, &GEP);

        A.addSuccessor(&B);
        B.addSuccessor(&S);
        S.addSuccessor(&GEP);
        GEP.addSuccessor(&L);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L.doesPointsTo(&A), "L do not points to A");
    }

    void load_from_zeroed()
    {
        using namespace analysis;

        PSNode B(pta::ALLOC);
        B.setZeroInitialized();
        PSNode L(pta::LOAD, &B);

        B.addSuccessor(&L);

        PointerSubgraph PS(&B);
        PTStoT PA(&PS);
        PA.run();

        check(L.doesPointsTo(pta::NULLPTR), "L do not points to nullptr");
    }

    void load_from_unknown_offset()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        PSNode B(pta::ALLOC);
        B.setSize(20);
        PSNode GEP(pta::GEP, &B, UNKNOWN_OFFSET);
        PSNode S(pta::STORE, &A, &GEP);
        PSNode GEP2(pta::GEP, &B, 4);
        PSNode L(pta::LOAD, &GEP2); // load from B + 4

        A.addSuccessor(&B);
        B.addSuccessor(&GEP);
        GEP.addSuccessor(&S);
        S.addSuccessor(&GEP2);
        GEP2.addSuccessor(&L);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        // B points to A + 0 at unknown offset,
        // so load from B + 4 should be A + 0
        check(L.doesPointsTo(&A), "L do not points to A");
    }

    void load_from_unknown_offset2()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        PSNode B(pta::ALLOC);
        B.setSize(20);
        PSNode GEP(pta::GEP, &B, 4);
        PSNode S(pta::STORE, &A, &GEP);
        PSNode GEP2(pta::GEP, &B, UNKNOWN_OFFSET);
        PSNode L(pta::LOAD, &GEP2); // load from B + UNKNOWN_OFFSET

        A.addSuccessor(&B);
        B.addSuccessor(&GEP);
        GEP.addSuccessor(&S);
        S.addSuccessor(&GEP2);
        GEP2.addSuccessor(&L);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        // B points to A + 0 at offset 4,
        // so load from B + UNKNOWN should be A + 0
        check(L.doesPointsTo(&A), "L do not points to A");
    }

    void load_from_unknown_offset3()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        PSNode B(pta::ALLOC);
        B.setSize(20);
        PSNode GEP(pta::GEP, &B, UNKNOWN_OFFSET);
        PSNode S(pta::STORE, &A, &GEP);
        PSNode GEP2(pta::GEP, &B, UNKNOWN_OFFSET);
        PSNode L(pta::LOAD, &GEP2);

        A.addSuccessor(&B);
        B.addSuccessor(&GEP);
        GEP.addSuccessor(&S);
        S.addSuccessor(&GEP2);
        GEP2.addSuccessor(&L);

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L.doesPointsTo(&A), "L do not points to A");
    }

    void memcpy_test()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        A.setSize(20);
        PSNode SRC(pta::ALLOC);
        SRC.setSize(16);
        PSNode DEST(pta::ALLOC);
        DEST.setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3 and A + 12
         * at offsets 4 and 8 */
        PSNode GEP1(pta::GEP, &A, 3);
        PSNode GEP2(pta::GEP, &A, 12);
        PSNode G1(pta::GEP, &SRC, 4);
        PSNode G2(pta::GEP, &SRC, 8);
        PSNode S1(pta::STORE, &GEP1, &G1);
        PSNode S2(pta::STORE, &GEP2, &G2);

        /* copy the memory,
         * after this node dest should point to
         * A + 3 and A + 12 at offsets 4 and 8 */
        PSNode CPY(pta::MEMCPY, &SRC, &DEST,
                    0 /* from 0 */, UNKNOWN_OFFSET /* len = all */);

        /* load from the dest memory */
        PSNode G3(pta::GEP, &DEST, 4);
        PSNode G4(pta::GEP, &DEST, 8);
        PSNode L1(pta::LOAD, &G3);
        PSNode L2(pta::LOAD, &G4);

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

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L1.doesPointsTo(&A, 3), "L do not points to A + 3");
        check(L2.doesPointsTo(&A, 12), "L do not points to A + 12");
    }

    void memcpy_test2()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        A.setSize(20);
        PSNode SRC(pta::ALLOC);
        SRC.setSize(16);
        PSNode DEST(pta::ALLOC);
        DEST.setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3 and A + 12
         * at offsets 4 and 8 */
        PSNode GEP1(pta::GEP, &A, 3);
        PSNode GEP2(pta::GEP, &A, 12);
        PSNode G1(pta::GEP, &SRC, 4);
        PSNode G2(pta::GEP, &SRC, 8);
        PSNode S1(pta::STORE, &GEP1, &G1);
        PSNode S2(pta::STORE, &GEP2, &G2);

        /* copy first 8 bytes from the memory,
         * after this node dest should point to
         * A + 3 at offset 4 (8 is 9th byte,
         * so it should not be included) */
        PSNode CPY(pta::MEMCPY, &SRC, &DEST,
                    0 /* from 0 */, 8 /* len*/);

        /* load from the dest memory */
        PSNode G3(pta::GEP, &DEST, 4);
        PSNode G4(pta::GEP, &DEST, 8);
        PSNode L1(pta::LOAD, &G3);
        PSNode L2(pta::LOAD, &G4);

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

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L1.doesPointsTo(&A, 3), "L1 do not points to A + 3");
        check(L2.pointsTo.empty(), "L2 is not empty");
    }

    void memcpy_test3()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        A.setSize(20);
        PSNode SRC(pta::ALLOC);
        SRC.setSize(16);
        PSNode DEST(pta::ALLOC);
        DEST.setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3 and A + 12
         * at offsets 4 and 8 */
        PSNode GEP1(pta::GEP, &A, 3);
        PSNode GEP2(pta::GEP, &A, 12);
        PSNode G1(pta::GEP, &SRC, 4);
        PSNode G2(pta::GEP, &SRC, 8);
        PSNode S1(pta::STORE, &GEP1, &G1);
        PSNode S2(pta::STORE, &GEP2, &G2);

        /* copy memory from 8 bytes and further
         * after this node dest should point to
         * A + 12 at offset 8 */
        PSNode CPY(pta::MEMCPY, &SRC, &DEST,
                    8 /* from */, UNKNOWN_OFFSET /* len*/);

        /* load from the dest memory */
        PSNode G3(pta::GEP, &DEST, 4);
        PSNode G4(pta::GEP, &DEST, 8);
        PSNode L1(pta::LOAD, &G3);
        PSNode L2(pta::LOAD, &G4);

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

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L2.doesPointsTo(&A, 12), "L2 do not points to A + 12");
        check(L1.pointsTo.empty(), "L1 is not empty");
    }

    void memcpy_test4()
    {
        using namespace analysis;

        PSNode A(pta::ALLOC);
        A.setSize(20);
        PSNode SRC(pta::ALLOC);
        SRC.setSize(16);
        SRC.setZeroInitialized();

        PSNode DEST(pta::ALLOC);
        DEST.setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3
         * offsets 4 */
        PSNode GEP1(pta::GEP, &A, 3);
        PSNode G1(pta::GEP, &SRC, 4);
        PSNode S1(pta::STORE, &GEP1, &G1);

        /* copy memory from 8 bytes and further
         * after this node dest should point to
         * A + 12 at offset 8 */
        PSNode CPY(pta::MEMCPY, &SRC, &DEST,
                    8 /* from */, UNKNOWN_OFFSET /* len*/);

        /* load from the dest memory */
        PSNode G3(pta::GEP, &SRC, 8);
        PSNode G4(pta::GEP, &DEST, 8);
        PSNode L1(pta::LOAD, &G3);
        PSNode L2(pta::LOAD, &G4);

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

        PointerSubgraph PS(&A);
        PTStoT PA(&PS);
        PA.run();

        check(L1.doesPointsTo(pta::NULLPTR), "L1 does not point to NULL");
        check(L2.doesPointsTo(pta::NULLPTR), "L2 does not point to NULL");
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
    : public PointsToTest<analysis::pta::PointsToFlowInsensitive>
{
public:
    FlowInsensitivePointsToTest()
        : PointsToTest<analysis::pta::PointsToFlowInsensitive>
          ("flow-insensitive points-to test") {}
};

class FlowSensitivePointsToTest
    : public PointsToTest<analysis::pta::PointsToFlowSensitive>
{
public:
    FlowSensitivePointsToTest()
        : PointsToTest<analysis::pta::PointsToFlowSensitive>
          ("flow-sensitive points-to test") {}
};

class PSNodeTest : public Test
{

public:
    PSNodeTest()
          : Test("PSNode test") {}

    void unknown_offset1()
    {
        using namespace dg::analysis::pta;
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

int main(void)
{
    using namespace dg::tests;
    TestRunner Runner;

    Runner.add(new FlowInsensitivePointsToTest());
    Runner.add(new FlowSensitivePointsToTest());
    Runner.add(new PSNodeTest());

    return Runner();
}
