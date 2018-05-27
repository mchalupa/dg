#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "analysis/PointsTo/PSNode.h"
#include "analysis/PointsTo/PointerSubgraph.h"
#include "analysis/PointsTo/Pointer.h"

using dg::analysis::pta::PSNode;
using dg::analysis::pta::PSNodeType;
using dg::analysis::pta::Pointer;
using dg::analysis::pta::PointerSubgraph;
using dg::analysis::pta::PointsToSet;

TEST_CASE("Querying empty set", "PointsToSet") {
    PointsToSet B;
    REQUIRE(B.empty());
    REQUIRE(B.size() == 0);
}

TEST_CASE("Add an element", "PointsToSet") {
    PointsToSet B;
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
    B.add(Pointer(A, 0));
    REQUIRE(*(B.begin()) == Pointer(A, 0));
}

TEST_CASE("Add few elements", "PointsToSet") {
    PointsToSet S;
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

TEST_CASE("Add few elements 2", "PointsToSet") {
    PointsToSet S;
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

TEST_CASE("Merge points-to sets", "PointsToSet") {
    PointsToSet S1;
    PointsToSet S2;
    PointerSubgraph PS;
    PSNode* A = PS.create(PSNodeType::ALLOC);
    PSNode* B = PS.create(PSNodeType::ALLOC);

    REQUIRE(S1.add({A, 0}));
    REQUIRE(S2.add({B, 0}));

    REQUIRE(S1.merge(S2));
    REQUIRE(S1.has({A, 0}));
    REQUIRE(S1.has({B, 0}));
    REQUIRE(S1.size() == 2);
}

