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
using dg::analysis::pta::DivisibleOffsetsPointsToSet;

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

TEST_CASE("Querying empty set", "PointsToSet") {
    queryingEmptySet<PointsToSet>();
    queryingEmptySet<SimplePointsToSet>();
    queryingEmptySet<SeparateOffsetsPointsToSet>();
    queryingEmptySet<SingleBitvectorPointsToSet>();
    queryingEmptySet<SmallOffsetsPointsToSet>();
    queryingEmptySet<DivisibleOffsetsPointsToSet>();
}

TEST_CASE("Add an element", "PointsToSet") {
    addAnElement<PointsToSet>();
    addAnElement<SimplePointsToSet>();
    addAnElement<SeparateOffsetsPointsToSet>();
    addAnElement<SingleBitvectorPointsToSet>();
    addAnElement<SmallOffsetsPointsToSet>();
    addAnElement<DivisibleOffsetsPointsToSet>();
}

TEST_CASE("Add few elements", "PointsToSet") {
    addFewElements<PointsToSet>();
    addFewElements<SimplePointsToSet>();
    addFewElements<SeparateOffsetsPointsToSet>();
    addFewElements<SingleBitvectorPointsToSet>();
    addFewElements<SmallOffsetsPointsToSet>();
    addFewElements<DivisibleOffsetsPointsToSet>();
}

TEST_CASE("Add few elements 2", "PointsToSet") {
    addFewElements2<PointsToSet>();
    addFewElements2<SimplePointsToSet>();
    addFewElements2<SeparateOffsetsPointsToSet>();
    addFewElements2<SingleBitvectorPointsToSet>();
    addFewElements2<SmallOffsetsPointsToSet>();
    addFewElements2<DivisibleOffsetsPointsToSet>();
}

TEST_CASE("Merge points-to sets", "PointsToSet") {
    mergePointsToSets<PointsToSet>();
    mergePointsToSets<SimplePointsToSet>();
    mergePointsToSets<SeparateOffsetsPointsToSet>();
    mergePointsToSets<SingleBitvectorPointsToSet>();
    mergePointsToSets<SmallOffsetsPointsToSet>();
    mergePointsToSets<DivisibleOffsetsPointsToSet>();
}