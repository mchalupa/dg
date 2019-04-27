#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "dg/analysis/PointsTo/PSNode.h"
#include "dg/analysis/PointsTo/PointerSubgraph.h"
#include "dg/analysis/PointsTo/Pointer.h"

using dg::analysis::pta::PSNode;
using dg::analysis::pta::PSNodeType;
using dg::analysis::pta::Pointer;
using dg::analysis::pta::PointerSubgraph;
using dg::analysis::pta::PointsToSet;
using dg::analysis::pta::SimplePointsToSet;
using dg::analysis::pta::SeparateOffsetsPointsToSet;
using dg::analysis::pta::SingleBitvectorPointsToSet;
using dg::analysis::pta::SmallOffsetsPointsToSet;
using dg::analysis::pta::AlignedOffsetsPointsToSet;
using dg::analysis::pta::AlignedBitvectorPointsToSet;

template<typename PTSetT>
void queryingEmptySet() {
    PTSetT B;
    REQUIRE(B.empty());
    REQUIRE(B.size() == 0);
}

template<typename PTSetT>
void addAnElement() {
    PTSetT B;
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
    B.add(Pointer(A, 0));
    REQUIRE(*(B.begin()) == Pointer(A, 0));
}

template<typename PTSetT>
void addFewElements() {
    PTSetT S;
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
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
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
    PSNode* B = PS.create(PSNodeType::ALLOC);
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
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
    PSNode* B = PS.create(PSNodeType::ALLOC);

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
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);

    REQUIRE(S.add({A, 0}) == true);
    REQUIRE(S.size() == 1);
    REQUIRE(S.remove({A, 0}) == true);
    REQUIRE(S.remove({A, 1}) == false);
    REQUIRE(S.size() == 0);
}

template<typename PTSetT>
void removeFewElements() {
    PTSetT S;
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
    PSNode* B = PS.create(PSNodeType::ALLOC);

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
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
    PSNode* B = PS.create(PSNodeType::ALLOC);

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
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
    PSNode* B = PS.create(PSNodeType::ALLOC);
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
    S.add(Pointer(A, dg::analysis::Offset::UNKNOWN));
    REQUIRE(S.pointsTo(Pointer(A, 0)) == false);
    REQUIRE(S.mayPointTo(Pointer(A, 0)) == true);
    REQUIRE(S.mustPointTo(Pointer(A, 0)) == false);
}

template<typename PTSetT>
void testAlignedOverflowBehavior() { //only works for aligned PTSets using overflow Set
    PTSetT S;
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
    PSNode* B = PS.create(PSNodeType::ALLOC);
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
    REQUIRE(S.add(Pointer(B, dg::analysis::Offset::UNKNOWN)) == true);
    REQUIRE(S.size() == 6);
    REQUIRE(S.overflowSetSize() == 2);
    REQUIRE(S.remove(Pointer(A, 11 * S.getMultiplier() + 1)) == true);
    REQUIRE(S.size() == 5);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.remove(Pointer(A, 2 * S.getMultiplier())) == true);
    REQUIRE(S.size() == 4);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.add(Pointer(A, dg::analysis::Offset::UNKNOWN)) == true);
    REQUIRE(S.size() == 2);
    REQUIRE(S.overflowSetSize() == 0);
    REQUIRE(S.removeAny(B) == true);
    REQUIRE(S.size() == 1);
    REQUIRE(S.overflowSetSize() == 0);
}

template<typename PTSetT>
void testSmallOverflowBehavior() { //only works for SmallOffsetsPTSet
    PTSetT S;
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
    PSNode* B = PS.create(PSNodeType::ALLOC);
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
    REQUIRE(S.add(Pointer(B, dg::analysis::Offset::UNKNOWN)) == true);
    REQUIRE(S.size() == 6);
    REQUIRE(S.overflowSetSize() == 2);
    REQUIRE(S.remove(Pointer(A, 63)) == true);
    REQUIRE(S.size() == 5);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.remove(Pointer(A, 62)) == true);
    REQUIRE(S.size() == 4);
    REQUIRE(S.overflowSetSize() == 1);
    REQUIRE(S.add(Pointer(A, dg::analysis::Offset::UNKNOWN)) == true);
    REQUIRE(S.size() == 2);
    REQUIRE(S.overflowSetSize() == 0);
    REQUIRE(S.removeAny(B) == true);
    REQUIRE(S.size() == 1);
    REQUIRE(S.overflowSetSize() == 0);
}

TEST_CASE("Querying empty set", "PointsToSet") {
    queryingEmptySet<PointsToSet>();
    queryingEmptySet<SimplePointsToSet>();
    queryingEmptySet<SeparateOffsetsPointsToSet>();
    queryingEmptySet<SingleBitvectorPointsToSet>();
    queryingEmptySet<SmallOffsetsPointsToSet>();
    queryingEmptySet<AlignedOffsetsPointsToSet>();
    queryingEmptySet<AlignedBitvectorPointsToSet>();
}

TEST_CASE("Add an element", "PointsToSet") {
    addAnElement<PointsToSet>();
    addAnElement<SimplePointsToSet>();
    addAnElement<SeparateOffsetsPointsToSet>();
    addAnElement<SingleBitvectorPointsToSet>();
    addAnElement<SmallOffsetsPointsToSet>();
    addAnElement<AlignedOffsetsPointsToSet>();
    addAnElement<AlignedBitvectorPointsToSet>();
}

TEST_CASE("Add few elements", "PointsToSet") {
    addFewElements<PointsToSet>();
    addFewElements<SimplePointsToSet>();
    addFewElements<SeparateOffsetsPointsToSet>();
    addFewElements<SingleBitvectorPointsToSet>();
    addFewElements<SmallOffsetsPointsToSet>();
    addFewElements<AlignedOffsetsPointsToSet>();
    addFewElements<AlignedBitvectorPointsToSet>();
}

TEST_CASE("Add few elements 2", "PointsToSet") {
    addFewElements2<PointsToSet>();
    addFewElements2<SimplePointsToSet>();
    addFewElements2<SeparateOffsetsPointsToSet>();
    addFewElements2<SingleBitvectorPointsToSet>();
    addFewElements2<SmallOffsetsPointsToSet>();
    addFewElements2<AlignedOffsetsPointsToSet>();
    addFewElements2<AlignedBitvectorPointsToSet>();
}

TEST_CASE("Merge points-to sets", "PointsToSet") {
    mergePointsToSets<PointsToSet>();
    mergePointsToSets<SimplePointsToSet>();
    mergePointsToSets<SeparateOffsetsPointsToSet>();
    mergePointsToSets<SingleBitvectorPointsToSet>();
    mergePointsToSets<SmallOffsetsPointsToSet>();
    mergePointsToSets<AlignedOffsetsPointsToSet>();
    mergePointsToSets<AlignedBitvectorPointsToSet>();
}

TEST_CASE("Remove element", "PointsToSet") { //SeparateOffsetsPointsToSet has different remove behavior, it isn't tested here
    removeElement<PointsToSet>();
    removeElement<SimplePointsToSet>();
    removeElement<SingleBitvectorPointsToSet>();
    removeElement<SmallOffsetsPointsToSet>();
    removeElement<AlignedOffsetsPointsToSet>();
    removeElement<AlignedBitvectorPointsToSet>();   
}

TEST_CASE("Remove few elements", "PointsToSet") { //SeparateOffsetsPointsToSet has different remove behavior, it isn't tested here
    removeFewElements<PointsToSet>();
    removeFewElements<SimplePointsToSet>();
    removeFewElements<SingleBitvectorPointsToSet>();
    removeFewElements<SmallOffsetsPointsToSet>();
    removeFewElements<AlignedOffsetsPointsToSet>();
    removeFewElements<AlignedBitvectorPointsToSet>();
}

TEST_CASE("Remove all elements pointing to a target", "PointsToSet") { //SeparateOffsetsPointsToSet has different behavior, it isn't tested here
    removeAnyTest<PointsToSet>();
    removeAnyTest<SimplePointsToSet>();
    removeAnyTest<SingleBitvectorPointsToSet>();
    removeAnyTest<SmallOffsetsPointsToSet>();
    removeAnyTest<AlignedOffsetsPointsToSet>();
    removeAnyTest<AlignedBitvectorPointsToSet>();
}

TEST_CASE("Test various points-to functions", "PointsToSet") {
    pointsToTest<PointsToSet>();
    pointsToTest<SimplePointsToSet>();
    pointsToTest<SeparateOffsetsPointsToSet>();
    pointsToTest<SingleBitvectorPointsToSet>();
    pointsToTest<SmallOffsetsPointsToSet>();
    pointsToTest<AlignedOffsetsPointsToSet>();
    pointsToTest<AlignedBitvectorPointsToSet>();
}

TEST_CASE("Test small overflow set behavior", "PointsToSet") {
    testSmallOverflowBehavior<SmallOffsetsPointsToSet>();
}

TEST_CASE("Test aligned overflow set behavior", "PointsToSet") {
    testAlignedOverflowBehavior<AlignedOffsetsPointsToSet>();
    testAlignedOverflowBehavior<AlignedBitvectorPointsToSet>();
}
