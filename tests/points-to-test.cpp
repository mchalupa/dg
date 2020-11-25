#include "catch.hpp"

#include "dg/PointerAnalysis/PointerGraph.h"
#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"

using namespace dg::pta;
using dg::Offset;

template <typename PTStoT>
void store_load() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    PSNode *S = PS.create<PSNodeType::STORE>(A, B);
    PSNode *L = PS.create<PSNodeType::LOAD>(B);

    A->addSuccessor(B);
    B->addSuccessor(S);
    S->addSuccessor(L);

    auto subg = PS.createSubgraph(A);
    PS.setEntry(subg);
    PTStoT PA(&PS);
    PA.run();

    REQUIRE(L->doesPointsTo(A));
}

template <typename PTStoT>
void store_load2() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    PSNode *C = PS.create<PSNodeType::ALLOC>();
    PSNode *S1 = PS.create<PSNodeType::STORE>(A, B);
    PSNode *S2 = PS.create<PSNodeType::STORE>(C, B);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(B);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(B);
    PSNode *L3 = PS.create<PSNodeType::LOAD>(B);

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

    REQUIRE(L1->doesPointsTo(A));
    REQUIRE(L2->doesPointsTo(C));
    REQUIRE(L3->doesPointsTo(A));
    REQUIRE(L3->doesPointsTo(C));
}

template <typename PTStoT>
void store_load3() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    PSNode *C = PS.create<PSNodeType::ALLOC>();
    PSNode *S1 = PS.create<PSNodeType::STORE>(A, B);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(B);
    PSNode *S2 = PS.create<PSNodeType::STORE>(C, B);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(B);

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

    REQUIRE(L1->doesPointsTo(A));
    REQUIRE(L2->doesPointsTo(C));
}

template <typename PTStoT>
void store_load4() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    A->setSize(8);
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    PSNode *C = PS.create<PSNodeType::ALLOC>();
    PSNode *GEP = PS.create<PSNodeType::GEP>(A, 4);
    PSNode *S1 = PS.create<PSNodeType::STORE>(GEP, B);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(B);
    PSNode *S2 = PS.create<PSNodeType::STORE>(C, B);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(B);

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

    REQUIRE(L1->doesPointsTo(A, 4));
    REQUIRE(L2->doesPointsTo(C));
}

template <typename PTStoT>
void store_load5() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    A->setSize(8);
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    B->setSize(16);
    PSNode *C = PS.create<PSNodeType::ALLOC>();
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(A, 4);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(B, 8);
    PSNode *S1 = PS.create<PSNodeType::STORE>(GEP1, GEP2);
    PSNode *GEP3 = PS.create<PSNodeType::GEP>(B, 8);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(GEP3);
    PSNode *S2 = PS.create<PSNodeType::STORE>(C, B);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(B);

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

    REQUIRE(L1->doesPointsTo(A, 4));
    REQUIRE(L2->doesPointsTo(C));
}

template <typename PTStoT>
void gep1() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    // we must set size, so that GEP won't
    // make the offset UNKNOWN
    A->setSize(8);
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(A, 4);
    PSNode *S = PS.create<PSNodeType::STORE>(B, GEP1);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(A, 4);
    PSNode *L = PS.create<PSNodeType::LOAD>(GEP2);

    A->addSuccessor(B);
    B->addSuccessor(GEP1);
    GEP1->addSuccessor(S);
    S->addSuccessor(GEP2);
    GEP2->addSuccessor(L);

    auto subg = PS.createSubgraph(A);
    PS.setEntry(subg);
    PTStoT PA(&PS);
    PA.run();

    REQUIRE(GEP1->doesPointsTo(A, 4));
    REQUIRE(GEP2->doesPointsTo(A, 4));
    REQUIRE(L->doesPointsTo(B));
}

template <typename PTStoT>
void gep2() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    A->setSize(16);
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(A, 4);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(GEP1, 4);
    PSNode *S = PS.create<PSNodeType::STORE>(B, GEP2);
    PSNode *GEP3 = PS.create<PSNodeType::GEP>(A, 8);
    PSNode *L = PS.create<PSNodeType::LOAD>(GEP3);

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

    REQUIRE(GEP1->doesPointsTo(A, 4));
    REQUIRE(L->doesPointsTo(B));
}

template <typename PTStoT>
void gep3() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    PSNode *ARRAY = PS.create<PSNodeType::ALLOC>();
    ARRAY->setSize(40);
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(ARRAY, 0);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(ARRAY, 4);
    PSNode *S1 = PS.create<PSNodeType::STORE>(A, GEP1);
    PSNode *S2 = PS.create<PSNodeType::STORE>(B, GEP2);
    PSNode *GEP3 = PS.create<PSNodeType::GEP>(ARRAY, 0);
    PSNode *GEP4 = PS.create<PSNodeType::GEP>(ARRAY, 4);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(GEP3);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(GEP4);

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

    REQUIRE(L1->doesPointsTo(A));
    REQUIRE(L2->doesPointsTo(B));
}

template <typename PTStoT>
void gep4() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    PSNodeAlloc *ARRAY = PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>());
    ARRAY->setSize(40);
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(ARRAY, 0);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(ARRAY, 4);
    PSNode *S1 = PS.create<PSNodeType::STORE>(A, GEP1);
    PSNode *S2 = PS.create<PSNodeType::STORE>(B, GEP2);
    PSNode *GEP3 = PS.create<PSNodeType::GEP>(ARRAY, 0);
    PSNode *GEP4 = PS.create<PSNodeType::GEP>(ARRAY, 4);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(GEP3);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(GEP4);

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

    REQUIRE(L1->doesPointsTo(A));
    REQUIRE(L2->doesPointsTo(B));
}

template <typename PTStoT>
void gep5() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    PSNode *ARRAY = PS.create<PSNodeType::ALLOC>();
    ARRAY->setSize(20);
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(ARRAY, 0);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(ARRAY, 4);
    PSNode *S1 = PS.create<PSNodeType::STORE>(A, GEP1);
    PSNode *S2 = PS.create<PSNodeType::STORE>(B, GEP2);
    PSNode *GEP3 = PS.create<PSNodeType::GEP>(ARRAY, 0);
    PSNode *GEP4 = PS.create<PSNodeType::GEP>(ARRAY, 4);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(GEP3);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(GEP4);
    PSNode *GEP5 = PS.create<PSNodeType::GEP>(ARRAY, 0);
    PSNode *S3 = PS.create<PSNodeType::STORE>(B, GEP5);
    PSNode *L3 = PS.create<PSNodeType::LOAD>(GEP5);

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

    REQUIRE(L1->doesPointsTo(A));
    REQUIRE(L1->doesPointsTo(B));
    REQUIRE(L2->doesPointsTo(B));
    REQUIRE(L3->doesPointsTo(B));
}

template <typename PTStoT>
void nulltest() {

    PointerGraph PS;
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    PSNode *S = PS.create<PSNodeType::STORE>(NULLPTR, B);
    PSNode *L = PS.create<PSNodeType::LOAD>(B);

    B->addSuccessor(S);
    S->addSuccessor(L);

    auto subg = PS.createSubgraph(B);
    PS.setEntry(subg);
    PTStoT PA(&PS);
    PA.run();

    REQUIRE(L->doesPointsTo(NULLPTR));
}

template <typename PTStoT>
void constant_store() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    B->setSize(16);
    PSNode *C = PS.create<PSNodeType::CONSTANT>(B, 4);
    PSNode *S = PS.create<PSNodeType::STORE>(A, C);
    PSNode *GEP = PS.create<PSNodeType::GEP>(B, 4);
    PSNode *L = PS.create<PSNodeType::LOAD>(GEP);

    A->addSuccessor(B);
    B->addSuccessor(S);
    S->addSuccessor(GEP);
    GEP->addSuccessor(L);

    auto subg = PS.createSubgraph(A);
    PS.setEntry(subg);
    PTStoT PA(&PS);
    PA.run();

    REQUIRE(L->doesPointsTo(A));
}

template <typename PTStoT>
void load_from_zeroed() {

    PointerGraph PS;
    PSNodeAlloc *B = PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>());
    B->setZeroInitialized();
    PSNode *L = PS.create<PSNodeType::LOAD>(B);

    B->addSuccessor(L);

    auto subg = PS.createSubgraph(B);
    PS.setEntry(subg);
    PTStoT PA(&PS);
    PA.run();

    REQUIRE(L->doesPointsTo(NULLPTR));
}

template <typename PTStoT>
void load_from_unknown_offset() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    B->setSize(20);
    PSNode *GEP = PS.create<PSNodeType::GEP>(B, Offset::UNKNOWN);
    PSNode *S = PS.create<PSNodeType::STORE>(A, GEP);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(B, 4);
    PSNode *L = PS.create<PSNodeType::LOAD>(GEP2); // load from B + 4

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
    REQUIRE(L->doesPointsTo(A));
}

template <typename PTStoT>
void load_from_unknown_offset2() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    B->setSize(20);
    PSNode *GEP = PS.create<PSNodeType::GEP>(B, 4);
    PSNode *S = PS.create<PSNodeType::STORE>(A, GEP);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(B, Offset::UNKNOWN);
    PSNode *L = PS.create<PSNodeType::LOAD>(GEP2); // load from B + Offset::UNKNOWN

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
    REQUIRE(L->doesPointsTo(A));
}

template <typename PTStoT>
void load_from_unknown_offset3() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    PSNode *B = PS.create<PSNodeType::ALLOC>();
    B->setSize(20);
    PSNode *GEP = PS.create<PSNodeType::GEP>(B, Offset::UNKNOWN);
    PSNode *S = PS.create<PSNodeType::STORE>(A, GEP);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(B, Offset::UNKNOWN);
    PSNode *L = PS.create<PSNodeType::LOAD>(GEP2);

    A->addSuccessor(B);
    B->addSuccessor(GEP);
    GEP->addSuccessor(S);
    S->addSuccessor(GEP2);
    GEP2->addSuccessor(L);

    auto subg = PS.createSubgraph(A);
    PS.setEntry(subg);
    PTStoT PA(&PS);
    PA.run();

    REQUIRE(L->doesPointsTo(A));
}

template <typename PTStoT>
void memcpy_test() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    A->setSize(20);
    PSNode *SRC = PS.create<PSNodeType::ALLOC>();
    SRC->setSize(16);
    PSNode *DEST = PS.create<PSNodeType::ALLOC>();
    DEST->setSize(16);

    /* initialize SRC, so that
     * it will point to A + 3 and A + 12
     * at offsets 4 and 8 */
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(A, 3);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(A, 12);
    PSNode *G1 = PS.create<PSNodeType::GEP>(SRC, 4);
    PSNode *G2 = PS.create<PSNodeType::GEP>(SRC, 8);
    PSNode *S1 = PS.create<PSNodeType::STORE>(GEP1, G1);
    PSNode *S2 = PS.create<PSNodeType::STORE>(GEP2, G2);

    /* copy the memory,
     * after this node dest should point to
     * A + 3 and A + 12 at offsets 4 and 8 */
    PSNode *CPY = PS.create<PSNodeType::MEMCPY>(
                          SRC, DEST, Offset::UNKNOWN /* len = all */);

    /* load from the dest memory */
    PSNode *G3 = PS.create<PSNodeType::GEP>(DEST, 4);
    PSNode *G4 = PS.create<PSNodeType::GEP>(DEST, 8);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(G3);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(G4);

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

    REQUIRE(L1->doesPointsTo(A, 3));
    REQUIRE(L2->doesPointsTo(A, 12));
}

template <typename PTStoT>
void memcpy_test2() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    A->setSize(20);
    PSNode *SRC = PS.create<PSNodeType::ALLOC>();
    SRC->setSize(16);
    PSNode *DEST = PS.create<PSNodeType::ALLOC>();
    DEST->setSize(16);

    /* initialize SRC, so that
     * it will point to A + 3 and A + 12
     * at offsets 4 and 8 */
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(A, 3);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(A, 12);
    PSNode *G1 = PS.create<PSNodeType::GEP>(SRC, 4);
    PSNode *G2 = PS.create<PSNodeType::GEP>(SRC, 8);
    PSNode *S1 = PS.create<PSNodeType::STORE>(GEP1, G1);
    PSNode *S2 = PS.create<PSNodeType::STORE>(GEP2, G2);

    /* copy first 8 bytes from the memory,
     * after this node dest should point to
     * A + 3 at offset 4  = PS.create<8 is 9th byte,
     * so it should not be included) */
    PSNode *CPY = PS.create<PSNodeType::MEMCPY>(SRC, DEST, 8 /* len*/);

    /* load from the dest memory */
    PSNode *G3 = PS.create<PSNodeType::GEP>(DEST, 4);
    PSNode *G4 = PS.create<PSNodeType::GEP>(DEST, 8);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(G3);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(G4);

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

    REQUIRE(L1->doesPointsTo(A, 3));
    REQUIRE(L2->pointsTo.empty());
}

template <typename PTStoT>
void memcpy_test3() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    A->setSize(20);
    PSNode *SRC = PS.create<PSNodeType::ALLOC>();
    SRC->setSize(16);
    PSNode *DEST = PS.create<PSNodeType::ALLOC>();
    DEST->setSize(16);

    /* initialize SRC, so that
     * it will point to A + 3 and A + 12
     * at offsets 4 and 8 */
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(A, 3);
    PSNode *GEP2 = PS.create<PSNodeType::GEP>(A, 12);
    PSNode *G1 = PS.create<PSNodeType::GEP>(SRC, 4);
    PSNode *G2 = PS.create<PSNodeType::GEP>(SRC, 8);
    PSNode *S1 = PS.create<PSNodeType::STORE>(GEP1, G1);
    PSNode *S2 = PS.create<PSNodeType::STORE>(GEP2, G2);

    /* copy memory from 8 bytes and further
     * after this node dest should point to
     * A + 12 at offset 0 */
    PSNode *CPY = PS.create<PSNodeType::MEMCPY>(
                          G2, DEST, Offset::UNKNOWN /* len*/);

    /* load from the dest memory */
    PSNode *G3 = PS.create<PSNodeType::GEP>(DEST, 4);
    PSNode *G4 = PS.create<PSNodeType::GEP>(DEST, 0);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(G3);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(G4);

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

    REQUIRE(L2->doesPointsTo(A, 12));
    REQUIRE(L1->pointsTo.empty());
}

template <typename PTStoT>
void memcpy_test4() {

    PointerGraph PS;
    PSNodeAlloc *A = PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>());
    A->setSize(20);
    PSNodeAlloc *SRC = PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>());
    SRC->setSize(16);
    SRC->setZeroInitialized();
    PSNode *DEST = PS.create<PSNodeType::ALLOC>();
    DEST->setSize(16);

    /* initialize SRC, so that it will point to A + 3 at offset 4 */
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(A, 3);
    PSNode *G1 = PS.create<PSNodeType::GEP>(SRC, 4);
    PSNode *S1 = PS.create<PSNodeType::STORE>(GEP1, G1);

    /* copy memory from 8 bytes and further after this node dest should
     * point to NULL */
    PSNode *G3 = PS.create<PSNodeType::GEP>(SRC, 8);
    PSNode *CPY = PS.create<PSNodeType::MEMCPY>(
                          G3, DEST, Offset::UNKNOWN /* len*/);

    /* load from the dest memory */
    PSNode *G4 = PS.create<PSNodeType::GEP>(DEST, 0);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(G3);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(G4);

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

    REQUIRE(L1->doesPointsTo(NULLPTR));
    REQUIRE(L2->doesPointsTo(NULLPTR));
}

template <typename PTStoT>
void memcpy_test5() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    A->setSize(20);
    PSNode *SRC = PS.create<PSNodeType::ALLOC>();
    SRC->setSize(16);
    PSNode *DEST = PS.create<PSNodeType::ALLOC>();
    DEST->setSize(16);

    PSNode *GEP1 = PS.create<PSNodeType::GEP>(A, 3);
    PSNode *G1 = PS.create<PSNodeType::GEP>(SRC, 4);
    PSNode *S1 = PS.create<PSNodeType::STORE>(GEP1, G1);

    // copy the only pointer to dest + 0
    PSNode *CPY = PS.create<PSNodeType::MEMCPY>(G1, DEST, 1);

    /* load from the dest memory */
    PSNode *G3 = PS.create<PSNodeType::GEP>(DEST, 0);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(G3);
    PSNode *G4 = PS.create<PSNodeType::GEP>(DEST, 1);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(G4);

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

    REQUIRE(L1->doesPointsTo(A, 3));
    REQUIRE(L2->pointsTo.empty());
}

template <typename PTStoT>
void memcpy_test6() {

    PointerGraph PS;
    PSNode *A = PS.create<PSNodeType::ALLOC>();
    A->setSize(20);
    PSNode *SRC = PS.create<PSNodeType::ALLOC>();
    SRC->setSize(16);
    PSNode *DEST = PS.create<PSNodeType::ALLOC>();
    DEST->setSize(16);

    PSNode *GEP1 = PS.create<PSNodeType::GEP>(A, 3);
    PSNode *G1 = PS.create<PSNodeType::GEP>(SRC, 4);
    PSNode *S1 = PS.create<PSNodeType::STORE>(GEP1, G1);
    PSNode *G3 = PS.create<PSNodeType::GEP>(DEST, 5);
    PSNode *G4 = PS.create<PSNodeType::GEP>(DEST, 1);

    PSNode *CPY = PS.create<PSNodeType::MEMCPY>(SRC, G4, 8);

    /* load from the dest memory */
    PSNode *L1 = PS.create<PSNodeType::LOAD>(G3);

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

    REQUIRE(L1->doesPointsTo(A, 3));
}

template <typename PTStoT>
void memcpy_test7() {

    PointerGraph PS;
    PSNodeAlloc *A = PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>());
    PSNodeAlloc *SRC = PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>());
    PSNode *DEST = PS.create<PSNodeType::ALLOC>();

    A->setSize(20);
    SRC->setSize(16);
    SRC->setZeroInitialized();
    DEST->setSize(16);

    PSNode *CPY = PS.create<PSNodeType::MEMCPY>(SRC, DEST,
                            Offset::UNKNOWN /* len*/);

    /* load from the dest memory */
    PSNode *G4 = PS.create<PSNodeType::GEP>(DEST, 0);
    PSNode *G5 = PS.create<PSNodeType::GEP>(DEST, 4);
    PSNode *G6 = PS.create<PSNodeType::GEP>(DEST, Offset::UNKNOWN);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(G4);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(G5);
    PSNode *L3 = PS.create<PSNodeType::LOAD>(G6);

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

    REQUIRE(L1->doesPointsTo(NULLPTR));
    REQUIRE(L2->doesPointsTo(NULLPTR));
    REQUIRE(L3->doesPointsTo(NULLPTR));
}

template <typename PTStoT>
void memcpy_test8() {

    PointerGraph PS;
    PSNodeAlloc *A = PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>());
    A->setSize(20);
    PSNodeAlloc *SRC = PSNodeAlloc::get(PS.create<PSNodeType::ALLOC>());
    SRC->setSize(16);
    SRC->setZeroInitialized();
    PSNode *DEST = PS.create<PSNodeType::ALLOC>();
    DEST->setSize(16);

    /* initialize SRC, so that it will point to A + 3 at offset 0 */
    PSNode *GEP1 = PS.create<PSNodeType::GEP>(A, 3);
    PSNode *S1 = PS.create<PSNodeType::STORE>(GEP1, SRC);

    PSNode *CPY = PS.create<PSNodeType::MEMCPY>(SRC, DEST, 10);

    /* load from the dest memory */
    PSNode *G1 = PS.create<PSNodeType::GEP>(DEST, 0);
    PSNode *G3 = PS.create<PSNodeType::GEP>(DEST, 4);
    PSNode *G4 = PS.create<PSNodeType::GEP>(DEST, 8);
    PSNode *L1 = PS.create<PSNodeType::LOAD>(G1);
    PSNode *L2 = PS.create<PSNodeType::LOAD>(G3);
    PSNode *L3 = PS.create<PSNodeType::LOAD>(G4);

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

    REQUIRE(L1->doesPointsTo(A, 3));
    REQUIRE(L2->doesPointsTo(NULLPTR));
    REQUIRE(L3->doesPointsTo(NULLPTR));
}

TEST_CASE("Flow insensitive", "FI") {
    store_load<dg::pta::PointerAnalysisFI>();
    store_load2<dg::pta::PointerAnalysisFI>();
    store_load3<dg::pta::PointerAnalysisFI>();
    store_load4<dg::pta::PointerAnalysisFI>();
    store_load5<dg::pta::PointerAnalysisFI>();
    gep1<dg::pta::PointerAnalysisFI>();
    gep2<dg::pta::PointerAnalysisFI>();
    gep3<dg::pta::PointerAnalysisFI>();
    gep4<dg::pta::PointerAnalysisFI>();
    gep5<dg::pta::PointerAnalysisFI>();
    nulltest<dg::pta::PointerAnalysisFI>();
    constant_store<dg::pta::PointerAnalysisFI>();
    load_from_zeroed<dg::pta::PointerAnalysisFI>();
    load_from_unknown_offset<dg::pta::PointerAnalysisFI>();
    load_from_unknown_offset2<dg::pta::PointerAnalysisFI>();
    load_from_unknown_offset3<dg::pta::PointerAnalysisFI>();
    memcpy_test<dg::pta::PointerAnalysisFI>();
    memcpy_test2<dg::pta::PointerAnalysisFI>();
    memcpy_test3<dg::pta::PointerAnalysisFI>();
    memcpy_test4<dg::pta::PointerAnalysisFI>();
    memcpy_test5<dg::pta::PointerAnalysisFI>();
    memcpy_test6<dg::pta::PointerAnalysisFI>();
    memcpy_test7<dg::pta::PointerAnalysisFI>();
    memcpy_test8<dg::pta::PointerAnalysisFI>();
}

TEST_CASE("Flow sensitive", "FS") {
    store_load<dg::pta::PointerAnalysisFS>();
    store_load2<dg::pta::PointerAnalysisFS>();
    store_load3<dg::pta::PointerAnalysisFS>();
    store_load4<dg::pta::PointerAnalysisFS>();
    store_load5<dg::pta::PointerAnalysisFS>();
    gep1<dg::pta::PointerAnalysisFS>();
    gep2<dg::pta::PointerAnalysisFS>();
    gep3<dg::pta::PointerAnalysisFS>();
    gep4<dg::pta::PointerAnalysisFS>();
    gep5<dg::pta::PointerAnalysisFS>();
    nulltest<dg::pta::PointerAnalysisFS>();
    constant_store<dg::pta::PointerAnalysisFS>();
    load_from_zeroed<dg::pta::PointerAnalysisFS>();
    load_from_unknown_offset<dg::pta::PointerAnalysisFS>();
    load_from_unknown_offset2<dg::pta::PointerAnalysisFS>();
    load_from_unknown_offset3<dg::pta::PointerAnalysisFS>();
    memcpy_test<dg::pta::PointerAnalysisFS>();
    memcpy_test2<dg::pta::PointerAnalysisFS>();
    memcpy_test3<dg::pta::PointerAnalysisFS>();
    memcpy_test4<dg::pta::PointerAnalysisFS>();
    memcpy_test5<dg::pta::PointerAnalysisFS>();
    memcpy_test6<dg::pta::PointerAnalysisFS>();
    memcpy_test7<dg::pta::PointerAnalysisFS>();
    memcpy_test8<dg::pta::PointerAnalysisFS>();
}

TEST_CASE("PSNode test", "PSNode") {
    using namespace dg::pta;
    PointerGraph PS;
    PSNode *N1 = PS.create<PSNodeType::ALLOC>();
    PSNode *N2 = PS.create<PSNodeType::LOAD>(N1);

    N2->addPointsTo(N1, 1);
    N2->addPointsTo(N1, 2);
    N2->addPointsTo(N1, 3);
    REQUIRE(N2->pointsTo.size() == 3);
    N2->addPointsTo(N1, Offset::UNKNOWN);
    REQUIRE(N2->pointsTo.size() == 1);
    REQUIRE(N2->addPointsTo(N1, 3) == false);
}

