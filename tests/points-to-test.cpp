#include <assert.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "test-runner.h"
#include "test-dg.h"

#include "dg/analysis/PointsTo/PointerGraph.h"
#include "dg/analysis/PointsTo/PointerAnalysisFI.h"
#include "dg/analysis/PointsTo/PointerAnalysisFS.h"

using namespace dg::pta;

namespace dg {
namespace tests {

template <typename PTStoT>
class PointsToTest : public Test
{
public:
    PointsToTest(const char *n) : Test(n) {}

    void store_load()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        PSNode *S = PS.create(PSNodeType::STORE, A, B);
        PSNode *L = PS.create(PSNodeType::LOAD, B);

        A->addSuccessor(B);
        B->addSuccessor(S);
        S->addSuccessor(L);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L->doesPointsTo(A), "L do not points to A");
    }

    void store_load2()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        PSNode *C = PS.create(PSNodeType::ALLOC);
        PSNode *S1 = PS.create(PSNodeType::STORE, A, B);
        PSNode *S2 = PS.create(PSNodeType::STORE, C, B);
        PSNode *L1 = PS.create(PSNodeType::LOAD, B);
        PSNode *L2 = PS.create(PSNodeType::LOAD, B);
        PSNode *L3 = PS.create(PSNodeType::LOAD, B);

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
        A->addSuccessor(B);
        B->addSuccessor(C);
        C->addSuccessor(S1);
        C->addSuccessor(S2);
        S1->addSuccessor(L1);
        S2->addSuccessor(L2);
        L1->addSuccessor(L3);
        L2->addSuccessor(L3);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A), "not L1->A");
        check(L2->doesPointsTo(C), "not L2->C");
        check(L3->doesPointsTo(A), "not L3->A");
        check(L3->doesPointsTo(C), "not L3->C");
    }

    void store_load3()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        PSNode *C = PS.create(PSNodeType::ALLOC);
        PSNode *S1 = PS.create(PSNodeType::STORE, A, B);
        PSNode *L1 = PS.create(PSNodeType::LOAD, B);
        PSNode *S2 = PS.create(PSNodeType::STORE, C, B);
        PSNode *L2 = PS.create(PSNodeType::LOAD, B);

        A->addSuccessor(B);
        B->addSuccessor(C);
        C->addSuccessor(S1);
        S1->addSuccessor(L1);
        L1->addSuccessor(S2);
        S2->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A), "L1 do not points to A");
        check(L2->doesPointsTo(C), "L2 do not points to C");
    }

    void store_load4()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        A->setSize(8);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        PSNode *C = PS.create(PSNodeType::ALLOC);
        PSNode *GEP = PS.create(PSNodeType::GEP, A, 4);
        PSNode *S1 = PS.create(PSNodeType::STORE, GEP, B);
        PSNode *L1 = PS.create(PSNodeType::LOAD, B);
        PSNode *S2 = PS.create(PSNodeType::STORE, C, B);
        PSNode *L2 = PS.create(PSNodeType::LOAD, B);

        A->addSuccessor(B);
        B->addSuccessor(C);
        C->addSuccessor(GEP);
        GEP->addSuccessor(S1);
        S1->addSuccessor(L1);
        L1->addSuccessor(S2);
        S2->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A, 4), "L1 do not points to A[4]");
        check(L2->doesPointsTo(C), "L2 do not points to C");
    }

    void store_load5()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        A->setSize(8);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        B->setSize(16);
        PSNode *C = PS.create(PSNodeType::ALLOC);
        PSNode *GEP1 = PS.create(PSNodeType::GEP, A, 4);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, B, 8);
        PSNode *S1 = PS.create(PSNodeType::STORE, GEP1, GEP2);
        PSNode *GEP3 = PS.create(PSNodeType::GEP, B, 8);
        PSNode *L1 = PS.create(PSNodeType::LOAD, GEP3);
        PSNode *S2 = PS.create(PSNodeType::STORE, C, B);
        PSNode *L2 = PS.create(PSNodeType::LOAD, B);

        A->addSuccessor(B);
        B->addSuccessor(C);
        C->addSuccessor(GEP1);
        GEP1->addSuccessor(GEP2);
        GEP2->addSuccessor(S1);
        S1->addSuccessor(GEP3);
        GEP3->addSuccessor(L1);
        L1->addSuccessor(S2);
        S2->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A, 4), "L1 do not points to A[4]");
        check(L2->doesPointsTo(C), "L2 do not points to C");
    }

    void gep1()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        // we must set size, so that GEP won't
        // make the offset UNKNOWN
        A->setSize(8);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        PSNode *GEP1 = PS.create(PSNodeType::GEP, A, 4);
        PSNode *S = PS.create(PSNodeType::STORE, B, GEP1);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, A, 4);
        PSNode *L = PS.create(PSNodeType::LOAD, GEP2);

        A->addSuccessor(B);
        B->addSuccessor(GEP1);
        GEP1->addSuccessor(S);
        S->addSuccessor(GEP2);
        GEP2->addSuccessor(L);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(GEP1->doesPointsTo(A, 4), "not GEP1 -> A + 4");
        check(GEP2->doesPointsTo(A, 4), "not GEP2 -> A + 4");
        check(L->doesPointsTo(B), "not L -> B + 0");
    }

    void gep2()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        A->setSize(16);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        PSNode *GEP1 = PS.create(PSNodeType::GEP, A, 4);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, GEP1, 4);
        PSNode *S = PS.create(PSNodeType::STORE, B, GEP2);
        PSNode *GEP3 = PS.create(PSNodeType::GEP, A, 8);
        PSNode *L = PS.create(PSNodeType::LOAD, GEP3);

        A->addSuccessor(B);
        B->addSuccessor(GEP1);
        GEP1->addSuccessor(GEP2);
        GEP2->addSuccessor(S);
        S->addSuccessor(GEP3);
        GEP3->addSuccessor(L);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(GEP1->doesPointsTo(A, 4), "not GEP1 -> A + 4");
        // XXX whata??
        //check(GEP2.doesPointsTo(&A, 8), "not GEP2 -> A + 4");
        //check(L.doesPointsTo(&B), "not L -> B + 0");
        check(L->doesPointsTo(B), "not L -> B + 0");
    }

    void gep3()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        PSNode *ARRAY = PS.create(PSNodeType::ALLOC);
        ARRAY->setSize(40);
        PSNode *GEP1 = PS.create(PSNodeType::GEP, ARRAY, 0);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, ARRAY, 4);
        PSNode *S1 = PS.create(PSNodeType::STORE, A, GEP1);
        PSNode *S2 = PS.create(PSNodeType::STORE, B, GEP2);
        PSNode *GEP3 = PS.create(PSNodeType::GEP, ARRAY, 0);
        PSNode *GEP4 = PS.create(PSNodeType::GEP, ARRAY, 4);
        PSNode *L1 = PS.create(PSNodeType::LOAD, GEP3);
        PSNode *L2 = PS.create(PSNodeType::LOAD, GEP4);

        A->addSuccessor(B);
        B->addSuccessor(ARRAY);
        ARRAY->addSuccessor(GEP1);
        GEP1->addSuccessor(GEP2);
        GEP2->addSuccessor(S1);
        S1->addSuccessor(S2);
        S2->addSuccessor(GEP3);
        GEP3->addSuccessor(GEP4);
        GEP4->addSuccessor(L1);
        L1->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A), "not L1->A");
        check(L2->doesPointsTo(B), "not L2->B");
    }

    void gep4()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        PSNodeAlloc *ARRAY = PSNodeAlloc::get(PS.create(PSNodeType::ALLOC));
        ARRAY->setSize(40);
        PSNode *GEP1 = PS.create(PSNodeType::GEP, ARRAY, 0);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, ARRAY, 4);
        PSNode *S1 = PS.create(PSNodeType::STORE, A, GEP1);
        PSNode *S2 = PS.create(PSNodeType::STORE, B, GEP2);
        PSNode *GEP3 = PS.create(PSNodeType::GEP, ARRAY, 0);
        PSNode *GEP4 = PS.create(PSNodeType::GEP, ARRAY, 4);
        PSNode *L1 = PS.create(PSNodeType::LOAD, GEP3);
        PSNode *L2 = PS.create(PSNodeType::LOAD, GEP4);

        A->addSuccessor(B);
        B->addSuccessor(ARRAY);
        ARRAY->addSuccessor(GEP1);
        ARRAY->addSuccessor(GEP2);

        GEP1->addSuccessor(S1);
        S1->addSuccessor(GEP3);

        GEP2->addSuccessor(S2);
        S2->addSuccessor(GEP3);

        GEP3->addSuccessor(GEP4);
        GEP4->addSuccessor(L1);
        L1->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A), "not L1->A");
        check(L2->doesPointsTo(B), "not L2->B");
    }

    void gep5()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        PSNode *ARRAY = PS.create(PSNodeType::ALLOC);
        ARRAY->setSize(20);
        PSNode *GEP1 = PS.create(PSNodeType::GEP, ARRAY, 0);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, ARRAY, 4);
        PSNode *S1 = PS.create(PSNodeType::STORE, A, GEP1);
        PSNode *S2 = PS.create(PSNodeType::STORE, B, GEP2);
        PSNode *GEP3 = PS.create(PSNodeType::GEP, ARRAY, 0);
        PSNode *GEP4 = PS.create(PSNodeType::GEP, ARRAY, 4);
        PSNode *L1 = PS.create(PSNodeType::LOAD, GEP3);
        PSNode *L2 = PS.create(PSNodeType::LOAD, GEP4);
        PSNode *GEP5 = PS.create(PSNodeType::GEP, ARRAY, 0);
        PSNode *S3 = PS.create(PSNodeType::STORE, B, GEP5);
        PSNode *L3 = PS.create(PSNodeType::LOAD, GEP5);

        A->addSuccessor(B);
        B->addSuccessor(ARRAY);
        ARRAY->addSuccessor(GEP1);
        ARRAY->addSuccessor(GEP2);

        GEP1->addSuccessor(S1);
        S1->addSuccessor(GEP3);

        GEP2->addSuccessor(S2);
        S2->addSuccessor(GEP5);
        GEP5->addSuccessor(S3);
        S3->addSuccessor(L3);
        L3->addSuccessor(GEP3);

        GEP3->addSuccessor(GEP4);
        GEP4->addSuccessor(L1);
        L1->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A), "not L1->A");
        check(L1->doesPointsTo(B), "not L1->B");
        check(L2->doesPointsTo(B), "not L2->B");
        check(L3->doesPointsTo(B), "not L2->B");
    }

    void nulltest()
    {

        PointerGraph PS;
        PSNode *B = PS.create(PSNodeType::ALLOC);
        PSNode *S = PS.create(PSNodeType::STORE, pta::NULLPTR, B);
        PSNode *L = PS.create(PSNodeType::LOAD, B);

        B->addSuccessor(S);
        S->addSuccessor(L);

        auto subg = PS.createSubgraph(B);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L->doesPointsTo(NULLPTR), "L do not points to NULL");
    }

    void constant_store()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        B->setSize(16);
        PSNode *C = PS.create(PSNodeType::CONSTANT, B, 4);
        PSNode *S = PS.create(PSNodeType::STORE, A, C);
        PSNode *GEP = PS.create(PSNodeType::GEP, B, 4);
        PSNode *L = PS.create(PSNodeType::LOAD, GEP);

        A->addSuccessor(B);
        B->addSuccessor(S);
        S->addSuccessor(GEP);
        GEP->addSuccessor(L);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L->doesPointsTo(A), "L do not points to A");
    }

    void load_from_zeroed()
    {

        PointerGraph PS;
        PSNodeAlloc *B = PSNodeAlloc::get(PS.create(PSNodeType::ALLOC));
        B->setZeroInitialized();
        PSNode *L = PS.create(PSNodeType::LOAD, B);

        B->addSuccessor(L);

        auto subg = PS.createSubgraph(B);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L->doesPointsTo(NULLPTR), "L do not points to nullptr");
    }

    void load_from_unknown_offset()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        B->setSize(20);
        PSNode *GEP = PS.create(PSNodeType::GEP, B, Offset::UNKNOWN);
        PSNode *S = PS.create(PSNodeType::STORE, A, GEP);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, B, 4);
        PSNode *L = PS.create(PSNodeType::LOAD, GEP2); // load from B + 4

        A->addSuccessor(B);
        B->addSuccessor(GEP);
        GEP->addSuccessor(S);
        S->addSuccessor(GEP2);
        GEP2->addSuccessor(L);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        // B points to A + 0 at unknown offset,
        // so load from B + 4 should be A + 0
        check(L->doesPointsTo(A), "L do not points to A");
    }

    void load_from_unknown_offset2()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        B->setSize(20);
        PSNode *GEP = PS.create(PSNodeType::GEP, B, 4);
        PSNode *S = PS.create(PSNodeType::STORE, A, GEP);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, B, Offset::UNKNOWN);
        PSNode *L = PS.create(PSNodeType::LOAD, GEP2); // load from B + Offset::UNKNOWN

        A->addSuccessor(B);
        B->addSuccessor(GEP);
        GEP->addSuccessor(S);
        S->addSuccessor(GEP2);
        GEP2->addSuccessor(L);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        // B points to A + 0 at offset 4,
        // so load from B + UNKNOWN should be A + 0
        check(L->doesPointsTo(A), "L do not points to A");
    }

    void load_from_unknown_offset3()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        PSNode *B = PS.create(PSNodeType::ALLOC);
        B->setSize(20);
        PSNode *GEP = PS.create(PSNodeType::GEP, B, Offset::UNKNOWN);
        PSNode *S = PS.create(PSNodeType::STORE, A, GEP);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, B, Offset::UNKNOWN);
        PSNode *L = PS.create(PSNodeType::LOAD, GEP2);

        A->addSuccessor(B);
        B->addSuccessor(GEP);
        GEP->addSuccessor(S);
        S->addSuccessor(GEP2);
        GEP2->addSuccessor(L);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L->doesPointsTo(A), "L do not points to A");
    }

    void memcpy_test()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        A->setSize(20);
        PSNode *SRC = PS.create(PSNodeType::ALLOC);
        SRC->setSize(16);
        PSNode *DEST = PS.create(PSNodeType::ALLOC);
        DEST->setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3 and A + 12
         * at offsets 4 and 8 */
        PSNode *GEP1 = PS.create(PSNodeType::GEP, A, 3);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, A, 12);
        PSNode *G1 = PS.create(PSNodeType::GEP, SRC, 4);
        PSNode *G2 = PS.create(PSNodeType::GEP, SRC, 8);
        PSNode *S1 = PS.create(PSNodeType::STORE, GEP1, G1);
        PSNode *S2 = PS.create(PSNodeType::STORE, GEP2, G2);

        /* copy the memory,
         * after this node dest should point to
         * A + 3 and A + 12 at offsets 4 and 8 */
        PSNode *CPY = PS.create(PSNodeType::MEMCPY, SRC, DEST,
                                Offset::UNKNOWN /* len = all */);

        /* load from the dest memory */
        PSNode *G3 = PS.create(PSNodeType::GEP, DEST, 4);
        PSNode *G4 = PS.create(PSNodeType::GEP, DEST, 8);
        PSNode *L1 = PS.create(PSNodeType::LOAD, G3);
        PSNode *L2 = PS.create(PSNodeType::LOAD, G4);

        A->addSuccessor(SRC);
        SRC->addSuccessor(DEST);
        DEST->addSuccessor(GEP1);
        GEP1->addSuccessor(GEP2);
        GEP2->addSuccessor(G1);
        G1->addSuccessor(G2);
        G2->addSuccessor(S1);
        S1->addSuccessor(S2);
        S2->addSuccessor(CPY);
        CPY->addSuccessor(G3);
        G3->addSuccessor(G4);
        G4->addSuccessor(L1);
        L1->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A, 3), "L do not points to A + 3");
        check(L2->doesPointsTo(A, 12), "L do not points to A + 12");
    }

    void memcpy_test2()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        A->setSize(20);
        PSNode *SRC = PS.create(PSNodeType::ALLOC);
        SRC->setSize(16);
        PSNode *DEST = PS.create(PSNodeType::ALLOC);
        DEST->setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3 and A + 12
         * at offsets 4 and 8 */
        PSNode *GEP1 = PS.create(PSNodeType::GEP, A, 3);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, A, 12);
        PSNode *G1 = PS.create(PSNodeType::GEP, SRC, 4);
        PSNode *G2 = PS.create(PSNodeType::GEP, SRC, 8);
        PSNode *S1 = PS.create(PSNodeType::STORE, GEP1, G1);
        PSNode *S2 = PS.create(PSNodeType::STORE, GEP2, G2);

        /* copy first 8 bytes from the memory,
         * after this node dest should point to
         * A + 3 at offset 4  = PS.create(8 is 9th byte,
         * so it should not be included) */
        PSNode *CPY = PS.create(PSNodeType::MEMCPY, SRC, DEST, 8 /* len*/);

        /* load from the dest memory */
        PSNode *G3 = PS.create(PSNodeType::GEP, DEST, 4);
        PSNode *G4 = PS.create(PSNodeType::GEP, DEST, 8);
        PSNode *L1 = PS.create(PSNodeType::LOAD, G3);
        PSNode *L2 = PS.create(PSNodeType::LOAD, G4);

        A->addSuccessor(SRC);
        SRC->addSuccessor(DEST);
        DEST->addSuccessor(GEP1);
        GEP1->addSuccessor(GEP2);
        GEP2->addSuccessor(G1);
        G1->addSuccessor(G2);
        G2->addSuccessor(S1);
        S1->addSuccessor(S2);
        S2->addSuccessor(CPY);
        CPY->addSuccessor(G3);
        G3->addSuccessor(G4);
        G4->addSuccessor(L1);
        L1->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A, 3), "L1 do not points to A + 3");
        check(L2->pointsTo.empty(), "L2 is not empty");
    }

    void memcpy_test3()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        A->setSize(20);
        PSNode *SRC = PS.create(PSNodeType::ALLOC);
        SRC->setSize(16);
        PSNode *DEST = PS.create(PSNodeType::ALLOC);
        DEST->setSize(16);

        /* initialize SRC, so that
         * it will point to A + 3 and A + 12
         * at offsets 4 and 8 */
        PSNode *GEP1 = PS.create(PSNodeType::GEP, A, 3);
        PSNode *GEP2 = PS.create(PSNodeType::GEP, A, 12);
        PSNode *G1 = PS.create(PSNodeType::GEP, SRC, 4);
        PSNode *G2 = PS.create(PSNodeType::GEP, SRC, 8);
        PSNode *S1 = PS.create(PSNodeType::STORE, GEP1, G1);
        PSNode *S2 = PS.create(PSNodeType::STORE, GEP2, G2);

        /* copy memory from 8 bytes and further
         * after this node dest should point to
         * A + 12 at offset 0 */
        PSNode *CPY = PS.create(PSNodeType::MEMCPY, G2, DEST,
                                Offset::UNKNOWN /* len*/);

        /* load from the dest memory */
        PSNode *G3 = PS.create(PSNodeType::GEP, DEST, 4);
        PSNode *G4 = PS.create(PSNodeType::GEP, DEST, 0);
        PSNode *L1 = PS.create(PSNodeType::LOAD, G3);
        PSNode *L2 = PS.create(PSNodeType::LOAD, G4);

        A->addSuccessor(SRC);
        SRC->addSuccessor(DEST);
        DEST->addSuccessor(GEP1);
        GEP1->addSuccessor(GEP2);
        GEP2->addSuccessor(G1);
        G1->addSuccessor(G2);
        G2->addSuccessor(S1);
        S1->addSuccessor(S2);
        S2->addSuccessor(CPY);
        CPY->addSuccessor(G3);
        G3->addSuccessor(G4);
        G4->addSuccessor(L1);
        L1->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L2->doesPointsTo(A, 12), "L2 do not points to A + 12");
        check(L1->pointsTo.empty(), "L1 is not empty");
    }

    void memcpy_test4()
    {

        PointerGraph PS;
        PSNodeAlloc *A = PSNodeAlloc::get(PS.create(PSNodeType::ALLOC));
        A->setSize(20);
        PSNodeAlloc *SRC = PSNodeAlloc::get(PS.create(PSNodeType::ALLOC));
        SRC->setSize(16);
        SRC->setZeroInitialized();
        PSNode *DEST = PS.create(PSNodeType::ALLOC);
        DEST->setSize(16);

        /* initialize SRC, so that it will point to A + 3 at offset 4 */
        PSNode *GEP1 = PS.create(PSNodeType::GEP, A, 3);
        PSNode *G1 = PS.create(PSNodeType::GEP, SRC, 4);
        PSNode *S1 = PS.create(PSNodeType::STORE, GEP1, G1);

        /* copy memory from 8 bytes and further after this node dest should
         * point to NULL */
        PSNode *G3 = PS.create(PSNodeType::GEP, SRC, 8);
        PSNode *CPY = PS.create(PSNodeType::MEMCPY, G3, DEST,
                                Offset::UNKNOWN /* len*/);

        /* load from the dest memory */
        PSNode *G4 = PS.create(PSNodeType::GEP, DEST, 0);
        PSNode *L1 = PS.create(PSNodeType::LOAD, G3);
        PSNode *L2 = PS.create(PSNodeType::LOAD, G4);

        A->addSuccessor(SRC);
        SRC->addSuccessor(DEST);
        DEST->addSuccessor(GEP1);
        GEP1->addSuccessor(G1);
        G1->addSuccessor(S1);
        S1->addSuccessor(G3);
        G3->addSuccessor(CPY);
        CPY->addSuccessor(G4);
        G4->addSuccessor(L1);
        L1->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(NULLPTR), "L1 does not point to NULL");
        check(L2->doesPointsTo(NULLPTR), "L2 does not point to NULL");
    }

    void memcpy_test5()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        A->setSize(20);
        PSNode *SRC = PS.create(PSNodeType::ALLOC);
        SRC->setSize(16);
        PSNode *DEST = PS.create(PSNodeType::ALLOC);
        DEST->setSize(16);

        PSNode *GEP1 = PS.create(PSNodeType::GEP, A, 3);
        PSNode *G1 = PS.create(PSNodeType::GEP, SRC, 4);
        PSNode *S1 = PS.create(PSNodeType::STORE, GEP1, G1);

        // copy the only pointer to dest + 0
        PSNode *CPY = PS.create(PSNodeType::MEMCPY, G1, DEST, 1);

        /* load from the dest memory */
        PSNode *G3 = PS.create(PSNodeType::GEP, DEST, 0);
        PSNode *L1 = PS.create(PSNodeType::LOAD, G3);
        PSNode *G4 = PS.create(PSNodeType::GEP, DEST, 1);
        PSNode *L2 = PS.create(PSNodeType::LOAD, G4);

        A->addSuccessor(SRC);
        SRC->addSuccessor(DEST);
        DEST->addSuccessor(GEP1);
        GEP1->addSuccessor(G1);
        G1->addSuccessor(S1);
        S1->addSuccessor(CPY);
        CPY->addSuccessor(G3);
        G3->addSuccessor(L1);
        L1->addSuccessor(G4);
        G4->addSuccessor(L2);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A, 3), "L2 do not points to A + 3");
        check(L2->pointsTo.empty(), "L2 does not have empty points-to");
    }

    void memcpy_test6()
    {

        PointerGraph PS;
        PSNode *A = PS.create(PSNodeType::ALLOC);
        A->setSize(20);
        PSNode *SRC = PS.create(PSNodeType::ALLOC);
        SRC->setSize(16);
        PSNode *DEST = PS.create(PSNodeType::ALLOC);
        DEST->setSize(16);

        PSNode *GEP1 = PS.create(PSNodeType::GEP, A, 3);
        PSNode *G1 = PS.create(PSNodeType::GEP, SRC, 4);
        PSNode *S1 = PS.create(PSNodeType::STORE, GEP1, G1);
        PSNode *G3 = PS.create(PSNodeType::GEP, DEST, 5);
        PSNode *G4 = PS.create(PSNodeType::GEP, DEST, 1);

        PSNode *CPY = PS.create(PSNodeType::MEMCPY, SRC, G4, 8);

        /* load from the dest memory */
        PSNode *L1 = PS.create(PSNodeType::LOAD, G3);

        A->addSuccessor(SRC);
        SRC->addSuccessor(DEST);
        DEST->addSuccessor(GEP1);
        GEP1->addSuccessor(G1);
        G1->addSuccessor(S1);
        S1->addSuccessor(G3);
        G3->addSuccessor(G4);
        G4->addSuccessor(CPY);
        CPY->addSuccessor(L1);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A, 3), "L2 do not points to A + 3");
    }

    void memcpy_test7()
    {

        PointerGraph PS;
        PSNodeAlloc *A = PSNodeAlloc::get(PS.create(PSNodeType::ALLOC));
        PSNodeAlloc *SRC = PSNodeAlloc::get(PS.create(PSNodeType::ALLOC));
        PSNode *DEST = PS.create(PSNodeType::ALLOC);

        A->setSize(20);
        SRC->setSize(16);
        SRC->setZeroInitialized();
        DEST->setSize(16);

        PSNode *CPY = PS.create(PSNodeType::MEMCPY, SRC, DEST,
                                Offset::UNKNOWN /* len*/);

        /* load from the dest memory */
        PSNode *G4 = PS.create(PSNodeType::GEP, DEST, 0);
        PSNode *G5 = PS.create(PSNodeType::GEP, DEST, 4);
        PSNode *G6 = PS.create(PSNodeType::GEP, DEST, Offset::UNKNOWN);
        PSNode *L1 = PS.create(PSNodeType::LOAD, G4);
        PSNode *L2 = PS.create(PSNodeType::LOAD, G5);
        PSNode *L3 = PS.create(PSNodeType::LOAD, G6);

        A->addSuccessor(SRC);
        SRC->addSuccessor(DEST);
        DEST->addSuccessor(CPY);
        CPY->addSuccessor(G4);
        G4->addSuccessor(G5);
        G5->addSuccessor(G6);
        G6->addSuccessor(L1);
        L1->addSuccessor(L2);
        L2->addSuccessor(L3);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(NULLPTR), "L1 does not point to NULL");
        check(L2->doesPointsTo(NULLPTR), "L2 does not point to NULL");
        check(L3->doesPointsTo(NULLPTR), "L2 does not point to NULL");
    }

    void memcpy_test8()
    {

        PointerGraph PS;
        PSNodeAlloc *A = PSNodeAlloc::get(PS.create(PSNodeType::ALLOC));
        A->setSize(20);
        PSNodeAlloc *SRC = PSNodeAlloc::get(PS.create(PSNodeType::ALLOC));
        SRC->setSize(16);
        SRC->setZeroInitialized();
        PSNode *DEST = PS.create(PSNodeType::ALLOC);
        DEST->setSize(16);

        /* initialize SRC, so that it will point to A + 3 at offset 0 */
        PSNode *GEP1 = PS.create(PSNodeType::GEP, A, 3);
        PSNode *S1 = PS.create(PSNodeType::STORE, GEP1, SRC);

        PSNode *CPY = PS.create(PSNodeType::MEMCPY, SRC, DEST, 10);

        /* load from the dest memory */
        PSNode *G1 = PS.create(PSNodeType::GEP, DEST, 0);
        PSNode *G3 = PS.create(PSNodeType::GEP, DEST, 4);
        PSNode *G4 = PS.create(PSNodeType::GEP, DEST, 8);
        PSNode *L1 = PS.create(PSNodeType::LOAD, G1);
        PSNode *L2 = PS.create(PSNodeType::LOAD, G3);
        PSNode *L3 = PS.create(PSNodeType::LOAD, G4);

        A->addSuccessor(SRC);
        SRC->addSuccessor(DEST);
        DEST->addSuccessor(GEP1);
        GEP1->addSuccessor(G1);
        G1->addSuccessor(S1);
        S1->addSuccessor(G3);
        G3->addSuccessor(CPY);
        CPY->addSuccessor(G4);
        G4->addSuccessor(L1);
        L1->addSuccessor(L2);
        L2->addSuccessor(L3);

        auto subg = PS.createSubgraph(A);
        PS.setEntry(subg);
        PTStoT PA(&PS);
        PA.run();

        check(L1->doesPointsTo(A, 3), "L1 does not point A + 3");
        check(L2->doesPointsTo(NULLPTR), "L2 does not point to NULL");
        check(L3->doesPointsTo(NULLPTR), "L3 does not point to NULL");
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
        memcpy_test5();
        memcpy_test6();
        memcpy_test7();
        memcpy_test8();
    }
};

class FlowInsensitivePointsToTest
    : public PointsToTest<pta::PointerAnalysisFI>
{
public:
    FlowInsensitivePointsToTest()
        : PointsToTest<pta::PointerAnalysisFI>
          ("flow-insensitive points-to test") {}
};

class FlowSensitivePointsToTest
    : public PointsToTest<pta::PointerAnalysisFS>
{
public:
    FlowSensitivePointsToTest()
        : PointsToTest<pta::PointerAnalysisFS>
          ("flow-sensitive points-to test") {}
};

class PSNodeTest : public Test
{

public:
    PSNodeTest()
          : Test("PSNode test") {}

    void unknown_offset1()
    {
        using namespace dg::pta;
        PointerGraph PS;
        PSNode *N1 = PS.create(PSNodeType::ALLOC);
        PSNode *N2 = PS.create(PSNodeType::LOAD, N1);

        N2->addPointsTo(N1, 1);
        N2->addPointsTo(N1, 2);
        N2->addPointsTo(N1, 3);
        check(N2->pointsTo.size() == 3);
        N2->addPointsTo(N1, Offset::UNKNOWN);
        check(N2->pointsTo.size() == 1);
        check(N2->addPointsTo(N1, 3) == false);
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
