#include "catch.hpp"

#include <dg/NodesWalk.h>
#include <dg/ADT/Queue.h>
#include <set>

using namespace dg;

struct Node {
    std::vector<Node *> _successors;
    const std::vector<Node *>& successors() const { return _successors; }
    void addSuccessor(Node *s) { _successors.push_back(s); }
};

TEST_CASE("NodesWalk1", "NodesWalk") {
    Node A, B, C, D;

    A.addSuccessor(&B);
    B.addSuccessor(&C);
    C.addSuccessor(&D);

    NodesWalk<Node, dg::ADT::QueueLIFO<Node*>> walk;

    std::set<Node *> nodes;
    walk.run(&A, [&nodes](Node *n) {
        bool ret = nodes.insert(n).second;
        REQUIRE(ret);
    });

    REQUIRE(nodes.count(&A) > 0);
    REQUIRE(nodes.count(&B) > 0);
    REQUIRE(nodes.count(&C) > 0);
    REQUIRE(nodes.count(&D) > 0);
}

TEST_CASE("NodesWalk-branch", "NodesWalk") {
    Node A, B, C, D;

    A.addSuccessor(&B);
    A.addSuccessor(&C);
    B.addSuccessor(&D);
    C.addSuccessor(&D);

    NodesWalk<Node, dg::ADT::QueueLIFO<Node*>> walk;

    std::set<Node *> nodes;
    walk.run(&A, [&nodes](Node *n) {
        bool ret = nodes.insert(n).second;
        REQUIRE(ret);
    });

    REQUIRE(nodes.count(&A) > 0);
    REQUIRE(nodes.count(&B) > 0);
    REQUIRE(nodes.count(&C) > 0);
    REQUIRE(nodes.count(&D) > 0);
}

TEST_CASE("NodesWalk-cycle", "NodesWalk") {
    Node A, B, C, D;

    A.addSuccessor(&B);
    A.addSuccessor(&C);
    B.addSuccessor(&D);
    C.addSuccessor(&D);
    D.addSuccessor(&A);

    NodesWalk<Node, dg::ADT::QueueLIFO<Node*>> walk;

    std::set<Node *> nodes;
    walk.run(&A, [&nodes](Node *n) {
        bool ret = nodes.insert(n).second;
        REQUIRE(ret);
    });

    REQUIRE(nodes.count(&A) > 0);
    REQUIRE(nodes.count(&B) > 0);
    REQUIRE(nodes.count(&C) > 0);
    REQUIRE(nodes.count(&D) > 0);
}

TEST_CASE("NodesWalk-cycle2", "NodesWalk") {
    Node pA, A, B, C, D;

    pA.addSuccessor(&A);
    A.addSuccessor(&B);
    A.addSuccessor(&C);
    B.addSuccessor(&D);
    C.addSuccessor(&D);
    D.addSuccessor(&A);

    NodesWalk<Node, dg::ADT::QueueLIFO<Node*>> walk;

    std::set<Node *> nodes;
    walk.run(&pA, [&nodes](Node *n) {
        bool ret = nodes.insert(n).second;
        REQUIRE(ret);
    });

    REQUIRE(nodes.count(&pA) > 0);
    REQUIRE(nodes.count(&A) > 0);
    REQUIRE(nodes.count(&B) > 0);
    REQUIRE(nodes.count(&C) > 0);
    REQUIRE(nodes.count(&D) > 0);
}


TEST_CASE("NodesWalk-disconnected", "NodesWalk") {
    Node A, B, C, D;

    A.addSuccessor(&B);
    A.addSuccessor(&C);
    D.addSuccessor(&A);

    NodesWalk<Node, dg::ADT::QueueLIFO<Node*>> walk;

    std::set<Node *> nodes;
    walk.run(&A, [&nodes](Node *n) {
        bool ret = nodes.insert(n).second;
        REQUIRE(ret);
    });

    REQUIRE(nodes.count(&A) > 0);
    REQUIRE(nodes.count(&B) > 0);
    REQUIRE(nodes.count(&C) > 0);
    REQUIRE(nodes.count(&D) == 0);
}

TEST_CASE("NodesWalk-disconnected2", "NodesWalk") {
    Node A, B, C, D;

    B.addSuccessor(&D);
    C.addSuccessor(&D);
    D.addSuccessor(&A);

    NodesWalk<Node, dg::ADT::QueueLIFO<Node*>> walk;

    std::set<Node *> nodes;
    walk.run(&A, [&nodes](Node *n) {
        bool ret = nodes.insert(n).second;
        REQUIRE(ret);
    });

    REQUIRE(nodes.count(&A) > 0);
    REQUIRE(nodes.count(&B) == 0);
    REQUIRE(nodes.count(&C) == 0);
    REQUIRE(nodes.count(&D) == 0);
}



#include <dg/BFS.h>

TEST_CASE("BFS-sanity", "BFS") {
    Node A, B, C, D;

    A.addSuccessor(&B);
    B.addSuccessor(&C);
    C.addSuccessor(&D);

    BFS<Node> bfs;

    std::set<Node *> nodes;
    bfs.run(&A, [&nodes](Node *n) {
        bool ret = nodes.insert(n).second;
        REQUIRE(ret);
    });

    REQUIRE(nodes.count(&A) > 0);
    REQUIRE(nodes.count(&B) > 0);
    REQUIRE(nodes.count(&C) > 0);
    REQUIRE(nodes.count(&D) > 0);
}

TEST_CASE("BFS-order", "BFS") {
    Node A, B, C, D, E;

    A.addSuccessor(&B);
    A.addSuccessor(&C);
    B.addSuccessor(&D);
    D.addSuccessor(&E);

    BFS<Node> bfs;

    std::vector<Node *> nodes;
    bfs.run(&A, [&nodes](Node *n) {
        nodes.push_back(n);
    });

    // here we know that the successors are processed
    // from left-to-right
    REQUIRE(nodes == decltype(nodes){&A, &B, &C, &D, &E});
}

TEST_CASE("BFS-order2", "BFS") {
    Node A, B, C, D, E, F;

    A.addSuccessor(&B);
    A.addSuccessor(&C);
    B.addSuccessor(&D);
    B.addSuccessor(&F);
    C.addSuccessor(&E);

    BFS<Node> bfs;

    std::vector<Node *> nodes;
    bfs.run(&A, [&nodes](Node *n) {
        nodes.push_back(n);
    });

    // here we know that the successors are processed
    // from left-to-right
    REQUIRE(nodes == decltype(nodes){&A, &B, &C, &D, &F, &E});
}

TEST_CASE("BFS-order3", "BFS") {
    Node A, B, C, D, E, F;

    A.addSuccessor(&B);
    A.addSuccessor(&C);
    B.addSuccessor(&D);
    B.addSuccessor(&F);
    D.addSuccessor(&E);

    BFS<Node> bfs;

    std::vector<Node *> nodes;
    bfs.run(&A, [&nodes](Node *n) {
        nodes.push_back(n);
    });

    // here we know that the successors are processed
    // from left-to-right
    REQUIRE(nodes == decltype(nodes){&A, &B, &C, &D, &F, &E});
}




#include <dg/DFS.h>

TEST_CASE("DFS1", "DFS") {
    Node A, B, C, D;

    A.addSuccessor(&B);
    B.addSuccessor(&C);
    C.addSuccessor(&D);

    DFS<Node> dfs;

    std::set<Node *> nodes;
    dfs.run(&A, [&nodes](Node *n) {
        bool ret = nodes.insert(n).second;
        REQUIRE(ret);
    });

    REQUIRE(nodes.count(&A) > 0);
    REQUIRE(nodes.count(&B) > 0);
    REQUIRE(nodes.count(&C) > 0);
    REQUIRE(nodes.count(&D) > 0);
}

TEST_CASE("DFS-order", "DFS") {
    Node A, B, C, D, E;

    A.addSuccessor(&B);
    A.addSuccessor(&C);
    B.addSuccessor(&D);
    D.addSuccessor(&E);

    DFS<Node> dfs;

    std::vector<Node *> nodes;
    dfs.run(&A, [&nodes](Node *n) {
        nodes.push_back(n);
    });

    // here we know that the successors are processed
    // from right-to-left
    REQUIRE(nodes == decltype(nodes){&A, &C, &B, &D, &E});
}

TEST_CASE("DFS-order2", "DFS") {
    Node A, B, C, D, E, F;

    A.addSuccessor(&B);
    A.addSuccessor(&C);
    B.addSuccessor(&D);
    B.addSuccessor(&F);
    D.addSuccessor(&E);

    DFS<Node> dfs;

    std::vector<Node *> nodes;
    dfs.run(&A, [&nodes](Node *n) {
        nodes.push_back(n);
    });

    // here we know that the successors are processed
    // from right-to-left
    REQUIRE(nodes == decltype(nodes){&A, &C, &B, &F, &D, &E});
}

TEST_CASE("DFS-order3", "DFS") {
    Node A, B, C, D, E, F, G;

    A.addSuccessor(&B);
    A.addSuccessor(&C);
    B.addSuccessor(&D);
    B.addSuccessor(&F);
    D.addSuccessor(&E);
    F.addSuccessor(&G);

    DFS<Node> dfs;

    std::vector<Node *> nodes;
    dfs.run(&A, [&nodes](Node *n) {
        nodes.push_back(n);
    });

    // here we know that the successors are processed
    // from right-to-left
    REQUIRE(nodes == decltype(nodes){&A, &C, &B, &F, &G, &D, &E});
}
