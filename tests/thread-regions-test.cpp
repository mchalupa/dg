#include "catch.hpp"
#include "../include/dg/llvm/analysis/ThreadRegions/ControlFlowGraph.h"
#include "../lib/llvm/analysis/ThreadRegions/include/Nodes/ArtificialNode.h"
#include "../lib/llvm/analysis/ThreadRegions/include/Nodes/ForkNode.h"
#include "../lib/llvm/analysis/ThreadRegions/include/Nodes/JoinNode.h"
#include "../lib/llvm/analysis/ThreadRegions/include/Nodes/ExitNode.h"
#include "../lib/llvm/analysis/ThreadRegions/include/Nodes/EntryNode.h"
#include "../lib/llvm/analysis/ThreadRegions/include/Nodes/ForkNode.h"
#include "../lib/llvm/analysis/ThreadRegions/include/Nodes/ReturnNode.h"
#include "../lib/llvm/analysis/ThreadRegions/include/Nodes/EndifNode.h"

#include <string>

TEST_CASE("Test of node class methods", "[node]") {
    Node * node0 = new ArtificialNode(nullptr);
    Node * node1 = new ArtificialNode(nullptr);


    SECTION("Incrementing Ids") {
        REQUIRE(node0->id() < node1->id());
    }

    SECTION("Name of node is set properly") {
        node0->setName("nodeA");
        REQUIRE(node0->name() == "nodeA");
        std::string dotname = "NODE" + std::to_string(node0->id());
        REQUIRE(node0->dotName() == dotname);

    }

    SECTION("add new successor increases size of successors"
            " of node0 and size of predecessors of node 1") {
        REQUIRE(node0->addSuccessor(node1));
        REQUIRE(node0->successors().size() == 1);
        REQUIRE(node1->predecessors().size() == 1);

        SECTION("adding the same successor for the second time does nothing") {
            REQUIRE_FALSE(node0->addSuccessor(node1));
            REQUIRE(node0->successors().size() == 1);
            REQUIRE(node1->predecessors().size() == 1);
        }
    }


    SECTION("Removing node1 from successors of node0"
            " decreases size of successors of node0 and predecessors of node1") {
        REQUIRE(node0->successors().empty());
        REQUIRE(node0->addSuccessor(node1));
        auto foundNodeIterator = node0->successors().find(node1);
        REQUIRE_FALSE(foundNodeIterator == node0->successors().end());
        REQUIRE(node0->successors().size() == 1);
        REQUIRE(node0->removeSuccessor(node1));
        REQUIRE(node0->successors().empty());
    }

    SECTION("Removing nonexistent successor does nothing") {
        REQUIRE(node0->successors().empty());
        REQUIRE_FALSE(node0->removeSuccessor(node1));
        REQUIRE(node0->successors().empty());
        Node * node2 = new ArtificialNode(nullptr);
        REQUIRE(node0->addSuccessor(node1));
        REQUIRE(node0->successors().size() == 1);
        REQUIRE_FALSE(node0->removeSuccessor(node2));
        REQUIRE(node0->successors().size() == 1);
    }

    SECTION("add new predecessor increases size of predecessors"
            " of node0 and size of successors of node 1") {
        REQUIRE(node0->predecessors().empty());
        REQUIRE(node0->addPredecessor(node1));
        REQUIRE(node0->predecessors().size() == 1);
        REQUIRE(node1->successors().size() == 1);

        SECTION("adding the same predecessor for the second time does nothing") {
            REQUIRE_FALSE(node0->addPredecessor(node1));
            REQUIRE(node0->predecessors().size() == 1);
            REQUIRE(node1->successors().size() == 1);
        }
    }

    SECTION("Removing node1 from predecessors of node0"
            " decreases size of successors of node0 and predecessors of node1") {
        REQUIRE(node0->successors().empty());
        REQUIRE(node0->addPredecessor(node1));
        auto foundNodeIterator = node0->predecessors().find(node1);
        REQUIRE_FALSE(foundNodeIterator == node0->predecessors().end());
        REQUIRE(node0->predecessors().size() == 1);
        REQUIRE(node0->removePredecessor(node1));
        REQUIRE(node0->successors().empty());
    }

    SECTION("Removing nonexistent predecessor does nothing") {
        REQUIRE(node0->predecessors().empty());
        REQUIRE_FALSE(node0->removePredecessor(node1));
        REQUIRE(node0->successors().empty());
        Node * node2 = new ArtificialNode(nullptr);
        REQUIRE(node0->addPredecessor(node1));
        REQUIRE(node0->predecessors().size() == 1);
        REQUIRE_FALSE(node0->removePredecessor(node2));
        REQUIRE(node0->predecessors().size() == 1);
    }

    SECTION("Adding nullptr as successor should return false") {
        REQUIRE(node0->successors().empty());
        REQUIRE_FALSE(node0->addSuccessor(nullptr));
        REQUIRE(node0->successors().empty());
    }

    SECTION("Adding nullptr as predecessor should return false") {
        REQUIRE(node0->predecessors().empty());
        REQUIRE_FALSE(node0->addPredecessor(nullptr));
        REQUIRE(node0->predecessors().empty());
    }

    SECTION("Removing nullptr from successors should return false") {
        REQUIRE(node0->successors().empty());
        REQUIRE_FALSE(node0->removeSuccessor(nullptr));
        REQUIRE(node0->successors().empty());
    }

    SECTION("Removing nullptr from predecessors should return false") {
        REQUIRE(node0->predecessors().empty());
        REQUIRE_FALSE(node0->removePredecessor(nullptr));
        REQUIRE(node0->predecessors().empty());
    }

    SECTION("Setting DfsState") {
        REQUIRE(node0->dfsState() == DfsState::UNDISCOVERED);
        node0->setDfsState(DfsState::DISCOVERED);
        REQUIRE(node0->dfsState() == DfsState::DISCOVERED);
        node0->setDfsState(DfsState::EXAMINED);
        REQUIRE(node0->dfsState() == DfsState::EXAMINED);
    }
}

TEST_CASE("Test of ThreadRegion class methods", "[ThreadRegion]") {
    ControlFlowGraph * controlFlowGraph = new ControlFlowGraph(nullptr, nullptr, "");
    ThreadRegion * threadRegion0 = new ThreadRegion(controlFlowGraph);
    ThreadRegion * threadRegion1 = new ThreadRegion(controlFlowGraph);

    REQUIRE(threadRegion0->successors().empty());
    REQUIRE(threadRegion1->successors().empty());

    SECTION("Incrementing Ids") {
        REQUIRE(threadRegion0->id() < threadRegion1->id());
    }

    SECTION("Name of node is set properly") {
        std::string dotname = "cluster" + std::to_string(threadRegion0->id());
        REQUIRE(dotname == threadRegion0->dotName());
    }

    SECTION("Add successor") {
        REQUIRE(threadRegion0->addSuccessor(threadRegion1));
        REQUIRE(threadRegion0->successors().size() == 1);
        REQUIRE(threadRegion1->predecessors().size() == 1);
    }

    SECTION("Add predecessor") {
        REQUIRE(threadRegion0->addPredecessor(threadRegion1));
        REQUIRE(threadRegion0->predecessors().size() == 1);
        REQUIRE(threadRegion1->successors().size() == 1);
    }

    SECTION("Remove existing successor") {
        threadRegion0->addSuccessor(threadRegion1);
        REQUIRE(threadRegion0->successors().size() == 1);
        REQUIRE(threadRegion0->removeSuccessor(threadRegion1));
        REQUIRE(threadRegion0->successors().empty());
    }

    SECTION("Removing existing predecessor") {
        threadRegion0->addPredecessor(threadRegion1);
        REQUIRE(threadRegion0->predecessors().size() == 1);
        REQUIRE(threadRegion0->removePredecessor(threadRegion1));
        REQUIRE(threadRegion0->predecessors().size() == 0);
    }

    SECTION("Removing nonexistent successor") {
        REQUIRE_FALSE(threadRegion0->removeSuccessor(threadRegion1));
    }

    SECTION("Removing nonexistent predecessor") {
        REQUIRE_FALSE(threadRegion0->removePredecessor(threadRegion1));
    }

    SECTION("Remove nullptr from successor") {
        REQUIRE_FALSE(threadRegion0->removeSuccessor(nullptr));
    }

    SECTION("Remove nullptr from predecessor") {
        REQUIRE_FALSE(threadRegion0->removePredecessor(nullptr));
    }

    SECTION("DfsState") {
        REQUIRE(threadRegion0->dfsState() == DfsState::DISCOVERED); // when we create thread region, we already discovered it
        REQUIRE(threadRegion1->dfsState() == DfsState::DISCOVERED);
        threadRegion0->setDfsState(DfsState::UNDISCOVERED);
        threadRegion1->setDfsState(DfsState::EXAMINED);
        REQUIRE(threadRegion0->dfsState() == DfsState::UNDISCOVERED);
        REQUIRE(threadRegion1->dfsState() == DfsState::EXAMINED);
    }
}

TEST_CASE("Test of interaction of ThreadRegion and Node", "[ThreadRegion-Node]") {
    ControlFlowGraph * controlFlowGraph = new ControlFlowGraph(nullptr, nullptr, "");
    ThreadRegion * threadRegion0 = new ThreadRegion(controlFlowGraph);
    Node * node0 = new ArtificialNode(controlFlowGraph);

    SECTION("Set existing thread region to node") {
        auto before = threadRegion0->nodes().size();
        node0->setThreadRegion(threadRegion0);
        REQUIRE(node0->threadRegion() == threadRegion0);
        auto after = threadRegion0->nodes().size();
        REQUIRE(before < after);
    }

    node0->setThreadRegion(threadRegion0);

    SECTION("Set nullptr as thread region") {
        auto before = node0->threadRegion();
        node0->setThreadRegion(nullptr);
        REQUIRE(node0->threadRegion() == before);
    }
}
