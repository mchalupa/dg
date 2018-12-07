#include <llvm/Support/raw_ostream.h>

#include <iostream>

#include "Node.h"
#include "JoinNode.h"
#include "ExitNode.h"
#include "ControlFlowGraph.h"

using namespace std;
using namespace llvm;

int Node::lastId = 0;

Node::Node(ControlFlowGraph *controlFlowGraph):id_(lastId++),
                                               controlFlowGraph_(controlFlowGraph) {}

int Node::id() const {
    return id_;
}

void Node::setName(string &name) { name_ = name; }

const string & Node::name() const { return name_; }
string   Node::name()       { return name_; }

string Node::dotName() const {
    return "NODE" + to_string(id_);
}

void Node::addPredecessor(Node *node) {
    predecessors_.insert(node);
    node->successors_.insert(this);
}

void Node::addSuccessor(Node *node) {
    successors_.insert(node);
    node->predecessors_.insert(this);
}

void Node::removePredecessor(Node *node) {
    predecessors_.erase(node);
    node->successors_.erase(this);
}

void Node::removeSuccessor(Node *node) {
    successors_.erase(node);
    node->predecessors_.erase(this);
}

const set<Node *> &Node::predecessors() const {
    return predecessors_;
}

const set<Node *> &Node::successors() const {
    return successors_;
}

bool Node::isArtificial() const {
    return false;
}

bool Node::isJoin() const {
    return false;
}

bool Node::isEntry() const {
    return false;
}

bool Node::isEndIf() const {
    return false;
}

bool Node::isFork() const {
    return false;
}

bool Node::isExit() const {
    return false;
}

void Node::printOutcomingEdges(ostream &ostream) const {
    for (const auto &successor : successors_) {
        ostream << this->dotName() << " -> " << successor->dotName() << "\n";
    }
}

void Node::setThreadRegion(ThreadRegion *threadRegion) {
    threadRegion_ = threadRegion;
    threadRegion_->insertNode(this);
}

ThreadRegion *Node::threadRegion() const {
    return threadRegion_;
}

ControlFlowGraph *Node::controlFlowGraph() {
    return controlFlowGraph_;
}

void Node::dfsVisit() {
    visitSuccessorsFromNode(successors(), this);
}

void Node::setDfsState(DfsState state) {
    dfsState_ = state;
}

DfsState Node::dfsState() { return dfsState_; }

bool shouldCreateNewRegion(const Node *caller, const Node *successor) {
    return caller->isExit()     ||
           caller->isFork()     ||
           successor->isEntry() ||
           successor->isEndIf() ||
           successor->isJoin();
}
