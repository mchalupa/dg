#include "catch.hpp"

#include "dg/PointerAnalysis/PSNode.h"
#include "dg/PointerAnalysis/PointerGraph.h"
#include "dg/PointerAnalysis/Pointer.h"

using namespace dg::pta;

template<typename PTSetT>
void queryingEmptySet() {
    PTSetT B;
    REQUIRE(B.empty());
    REQUIRE(B.size() == 0);
}

template<typename PTSetT>
void addAnElement() {
    PTSetT B;
    PointerGraph PS;
    PSNode* A = PS.create<PSNodeType::ALLOC>();
    B.add(Pointer(A, 0));
    REQUIRE(*(B.begin()) == Pointer(A, 0));
}

template<typename PTSetT>
void addFewElements() {
    PTSetT S;
    PointerGraph PS;
    PSNode* A = PS.create<PSNodeType::ALLOC>();
    REQUIRE(S.add(Pointer(A, 0)) == true);
    REQUIRE(S.add(Pointer(A, 20)) == true);
    REQUIRE(S.add(Pointer(A, 120)) == true);
    REQUIRE(S.add(Pointer(A, 1240)) == true);
    REQUIRE(S.add(Pointer(A, 235235)) == true);
    REQUIRE(S.add(Pointer(A, 22332435235)) == true);
    for (const auto& ptr : S)
    REQUIRE(ptr.target == A);
    REQUIRE(S.size() == 6);
}

template<typename PTSetT>
void addFewElements2() {
    PTSetT S;
    PointerGraph PS;
    PSNode* A = PS.create<PSNodeType::ALLOC>();
    PSNode* B = PS.create<PSNodeType::ALLOC>();
    REQUIRE(S.add(Pointer(A, 0)) == true);
    REQUIRE(S.add(Pointer(A, 20)) == true);
    REQUIRE(S.add(Pointer(A, 120)) == true);
    REQUIRE(S.add(Pointer(A, 1240)) == true);
    REQUIRE(S.add(Pointer(A, 235235)) == true);
    REQUIRE(S.add(Pointer(A, 22332435235)) == true);
    for (const auto& ptr : S)
        REQUIRE((ptr.target == A || ptr.target == B));

    REQUIRE(S.size() == 6);

    REQUIRE(S.add(Pointer(A, 0)) == false);
    REQUIRE(S.add(Pointer(A, 20)) == false);
    REQUIRE(S.add(Pointer(A, 120)) == false);
    REQUIRE(S.add(Pointer(A, 1240)) == false);
    REQUIRE(S.add(Pointer(A, 235235)) == false);
    REQUIRE(S.add(Pointer(A, 22332435235)) == false);

    REQUIRE(S.size() == 6);
}

template<typename PTSetT>
void mergePointsToSets() {
    PTSetT S1;
    PTSetT S2;
    PointerGraph PS;
    PSNode* A = PS.create<PSNodeType::ALLOC>();
    PSNode* B = PS.create<PSNodeType::ALLOC>();

    REQUIRE(S1.add({A, 0}));
    REQUIRE(S2.add({B, 0}));

    // union (merge) operation
    REQUIRE(S1.add(S2));
    REQUIRE(S1.has({A, 0}));
    REQUIRE(S1.has({B, 0}));
    REQUIRE(S1.size() == 2);
}

template<typename PTSetT>
void removeElement() {
    PTSetT S;
    PointerGraph PS;
    PSNode* A = PS.create<PSNodeType::ALLOC>();

    REQUIRE(S.add({A, 0}) == true);
    REQUIRE(S.size() == 1);
    REQUIRE(S.remove({A, 0}) == true);
    REQUIRE(S.remove({A, 1}) == false);
    REQUIRE(S.size() == 0);
}

template<typename PTSetT>
void removeFewElements() {
    PTSetT S;
    PointerGraph PS;
    PSNode* A = PS.create<PSNodeType::ALLOC>();
    PSNode* B = PS.create<PSNodeType::ALLOC>();

    REQUIRE(S.add(Pointer(A, 0)) == true);
    REQUIRE(S.add(Pointer(A, 16)) == true);
    REQUIRE(S.add(Pointer(A, 120)) == true);
    REQUIRE(S.add(Pointer(B, 1240)) == true);
    REQUIRE(S.add(Pointer(B, 235235)) == true);
    REQUIRE(S.add(Pointer(B, 22332435235)) == true);
    REQUIRE(S.remove({A, 0}) == true);
    REQUIRE(S.remove({A, 120}) == true);
    REQUIRE(S.remove({B, 22332435235}) == true);
    REQUIRE(S.size() == 3);
}

template<typename PTSetT>
void removeAnyTest() {
    PTSetT S;
    PointerGraph PS;
    PSNode* A = PS.create<PSNodeType::ALLOC>();
    PSNode* B = PS.create<PSNodeType::ALLOC>();

    REQUIRE(S.add(Pointer(A, 0)) == true);
    REQUIRE(S.add(Pointer(A, 16)) == true);
    REQUIRE(S.add(Pointer(A, 120)) == true);
    REQUIRE(S.add(Pointer(B, 1240)) == true);
    REQUIRE(S.add(Pointer(B, 235235)) == true);
    REQUIRE(S.add(Pointer(B, 22332435235)) == true);
    REQUIRE(S.removeAny(A) == true);
    REQUIRE(S.removeAny(A) == false);
    REQUIRE(S.size() == 3);
    REQUIRE(S.removeAny(B) == true);
    REQUIRE(S.size() == 0);
    REQUIRE(S.removeAny(B) == false);
}

template<typename PTSetT>
void pointsToTest() {
    PTSetT S;
    PointerGraph PS;
    PSNode* A = PS.create<PSNodeType::ALLOC>();
    PSNode* B = PS.create<PSNodeType::ALLOC>();
    S.add(Pointer(A, 0));
    REQUIRE(S.pointsTo(Pointer(A, 0)) == true);
    REQUIRE(S.mayPointTo(Pointer(A, 0)) == true);
    REQUIRE(S.mustPointTo(Pointer(A, 0)) == true);
    S.add(Pointer(A, 8));
    S.add(Pointer(A, 64));
    S.add(Pointer(B, 123));
    REQUIRE(S.pointsTo(Pointer(A, 0)) == true);
    REQUIRE(S.mayPointTo(Pointer(A, 0)) == true);
    REQUIRE(S.mustPointTo(Pointer(A, 0)) == false);
    REQUIRE(S.pointsTo(Pointer(A, 64)) == true);
    REQUIRE(S.mayPointTo(Pointer(A, 64)) == true);
    REQUIRE(S.mustPointTo(Pointer(A, 64)) == false);   
    REQUIRE(S.pointsTo(Pointer(B, 123)) == true);
    REQUIRE(S.mayPointTo(Pointer(B, 123)) == true);
    REQUIRE(S.mustPointTo(Pointer(B, 123)) == false);
    REQUIRE(S.mayPointTo(Pointer(A,10000)) == false);
    REQUIRE(S.mustPointTo(Pointer(A,10000)) == false);
    REQUIRE(S.pointsTo(Pointer(A,10000)) == false);
    S.add(Pointer(A, dg::Offset::UNKNOWN));
    REQUIRE(S.pointsTo(Pointer(A, 0)) == false);
    REQUIRE(S.mayPointTo(Pointer(A, 0)) == true);
    REQUIRE(S.mustPointTo(Pointer(A, 0)) == false);
}

template<typename PTSetT>
void testAlignedOverflowBehavior() { //only works for aligned PTSets using overflow Set
    PTSetT S;
    PointerGraph PS;
    PSNode* A = PS.create<PSNodeType::ALLOC>();
    PSNode* B = PS.create<PSNodeType::ALLOC>();
    REQUIRE(S.getMultiplier() > 1);
    REQUIRE(S.add(Pointer(A, 0)) == true);
    REQUIRE(S.size() == 1);
    REQUIRE(S.overflowSetSize() == 0);
    REQUIRE(S.add(Pointer(A, S.getMultiplier())) == true);
    REQUIRE(S.size() == 2);
    REQUIRE(S.overflowSetSize() == 0);
    REQUIRE(S.add(Pointer(A, 2 * S.getMultiplier() + 1)) == true);
    REQUIRE(S.size() == 3);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.add(Pointer(A, 2 * S.getMultiplier())) == true);
    REQUIRE(S.size() == 4);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.add(Pointer(A, 11 * S.getMultiplier() + 1)) == true);
    REQUIRE(S.size() == 5);
    REQUIRE(S.overflowSetSize() == 2);
    REQUIRE(S.add(Pointer(B, dg::Offset::UNKNOWN)) == true);
    REQUIRE(S.size() == 6);
    REQUIRE(S.overflowSetSize() == 2);
    REQUIRE(S.remove(Pointer(A, 11 * S.getMultiplier() + 1)) == true);
    REQUIRE(S.size() == 5);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.remove(Pointer(A, 2 * S.getMultiplier())) == true);
    REQUIRE(S.size() == 4);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.add(Pointer(A, dg::Offset::UNKNOWN)) == true);
    REQUIRE(S.size() == 2);
    REQUIRE(S.overflowSetSize() == 0);
    REQUIRE(S.removeAny(B) == true);
    REQUIRE(S.size() == 1);
    REQUIRE(S.overflowSetSize() == 0);
}

template<typename PTSetT>
void testSmallOverflowBehavior() { //only works for SmallOffsetsPTSet
    PTSetT S;
    PointerGraph PS;
    PSNode* A = PS.create<PSNodeType::ALLOC>();
    PSNode* B = PS.create<PSNodeType::ALLOC>();
    REQUIRE(S.add(Pointer(A, 0)) == true);
    REQUIRE(S.size() == 1);
    REQUIRE(S.overflowSetSize() == 0);
    REQUIRE(S.add(Pointer(A, 21)) == true);
    REQUIRE(S.size() == 2);
    REQUIRE(S.overflowSetSize() == 0);
    REQUIRE(S.add(Pointer(A, 63)) == true);
    REQUIRE(S.size() == 3);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.add(Pointer(A, 62)) == true);
    REQUIRE(S.size() == 4);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.add(Pointer(A, 1287)) == true);
    REQUIRE(S.size() == 5);
    REQUIRE(S.overflowSetSize() == 2);
    REQUIRE(S.add(Pointer(B, dg::Offset::UNKNOWN)) == true);
    REQUIRE(S.size() == 6);
    REQUIRE(S.overflowSetSize() == 2);
    REQUIRE(S.remove(Pointer(A, 63)) == true);
    REQUIRE(S.size() == 5);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.remove(Pointer(A, 62)) == true);
    REQUIRE(S.size() == 4);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.add(Pointer(A, dg::Offset::UNKNOWN)) == true);
    REQUIRE(S.size() == 2);
    REQUIRE(S.overflowSetSize() == 0);
    REQUIRE(S.removeAny(B) == true);
    REQUIRE(S.size() == 1);
    REQUIRE(S.overflowSetSize() == 0);
}

TEST_CASE("Querying empty set", "PointsToSet") {
    queryingEmptySet<OffsetsSetPointsToSet>();
    queryingEmptySet<SimplePointsToSet>();
    queryingEmptySet<SeparateOffsetsPointsToSet>();
    queryingEmptySet<PointerIdPointsToSet>();
    queryingEmptySet<SmallOffsetsPointsToSet>();
    queryingEmptySet<AlignedSmallOffsetsPointsToSet>();
    queryingEmptySet<AlignedPointerIdPointsToSet>();
}

TEST_CASE("Add an element", "PointsToSet") {
    addAnElement<OffsetsSetPointsToSet>();
    addAnElement<SimplePointsToSet>();
    addAnElement<SeparateOffsetsPointsToSet>();
    addAnElement<PointerIdPointsToSet>();
    addAnElement<SmallOffsetsPointsToSet>();
    addAnElement<AlignedSmallOffsetsPointsToSet>();
    addAnElement<AlignedPointerIdPointsToSet>();
}

TEST_CASE("Add few elements", "PointsToSet") {
    addFewElements<OffsetsSetPointsToSet>();
    addFewElements<SimplePointsToSet>();
    addFewElements<SeparateOffsetsPointsToSet>();
    addFewElements<PointerIdPointsToSet>();
    addFewElements<SmallOffsetsPointsToSet>();
    addFewElements<AlignedSmallOffsetsPointsToSet>();
    addFewElements<AlignedPointerIdPointsToSet>();
}

TEST_CASE("Add few elements 2", "PointsToSet") {
    addFewElements2<OffsetsSetPointsToSet>();
    addFewElements2<SimplePointsToSet>();
    addFewElements2<SeparateOffsetsPointsToSet>();
    addFewElements2<PointerIdPointsToSet>();
    addFewElements2<SmallOffsetsPointsToSet>();
    addFewElements2<AlignedSmallOffsetsPointsToSet>();
    addFewElements2<AlignedPointerIdPointsToSet>();
}

TEST_CASE("Merge points-to sets", "PointsToSet") {
    mergePointsToSets<OffsetsSetPointsToSet>();
    mergePointsToSets<SimplePointsToSet>();
    mergePointsToSets<SeparateOffsetsPointsToSet>();
    mergePointsToSets<PointerIdPointsToSet>();
    mergePointsToSets<SmallOffsetsPointsToSet>();
    mergePointsToSets<AlignedSmallOffsetsPointsToSet>();
    mergePointsToSets<AlignedPointerIdPointsToSet>();
}

TEST_CASE("Remove element", "PointsToSet") { //SeparateOffsetsPointsToSet has different remove behavior, it isn't tested here
    removeElement<OffsetsSetPointsToSet>();
    removeElement<SimplePointsToSet>();
    removeElement<PointerIdPointsToSet>();
    removeElement<SmallOffsetsPointsToSet>();
    removeElement<AlignedSmallOffsetsPointsToSet>();
    removeElement<AlignedPointerIdPointsToSet>();   
}

TEST_CASE("Remove few elements", "PointsToSet") { //SeparateOffsetsPointsToSet has different remove behavior, it isn't tested here
    removeFewElements<OffsetsSetPointsToSet>();
    removeFewElements<SimplePointsToSet>();
    removeFewElements<PointerIdPointsToSet>();
    removeFewElements<SmallOffsetsPointsToSet>();
    removeFewElements<AlignedSmallOffsetsPointsToSet>();
    removeFewElements<AlignedPointerIdPointsToSet>();
}

TEST_CASE("Remove all elements pointing to a target", "PointsToSet") { //SeparateOffsetsPointsToSet has different behavior, it isn't tested here
    removeAnyTest<OffsetsSetPointsToSet>();
    removeAnyTest<SimplePointsToSet>();
    removeAnyTest<PointerIdPointsToSet>();
    removeAnyTest<SmallOffsetsPointsToSet>();
    removeAnyTest<AlignedSmallOffsetsPointsToSet>();
    removeAnyTest<AlignedPointerIdPointsToSet>();
}

TEST_CASE("Test various points-to functions", "PointsToSet") {
    pointsToTest<OffsetsSetPointsToSet>();
    pointsToTest<SimplePointsToSet>();
    pointsToTest<SeparateOffsetsPointsToSet>();
    pointsToTest<PointerIdPointsToSet>();
    pointsToTest<SmallOffsetsPointsToSet>();
    pointsToTest<AlignedSmallOffsetsPointsToSet>();
    pointsToTest<AlignedPointerIdPointsToSet>();
}

TEST_CASE("Test small overflow set behavior", "PointsToSet") {
    testSmallOverflowBehavior<SmallOffsetsPointsToSet>();
}

TEST_CASE("Test aligned overflow set behavior", "PointsToSet") {
    testAlignedOverflowBehavior<AlignedSmallOffsetsPointsToSet>();
    testAlignedOverflowBehavior<AlignedPointerIdPointsToSet>();
}
