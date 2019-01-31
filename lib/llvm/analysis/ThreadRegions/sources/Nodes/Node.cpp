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

void Node::setName(const string &name) { name_ = name; }

const string & Node::name() const { return name_; }
string   Node::name()       { return name_; }

string Node::dotName() const {
    return "NODE" + to_string(id_);
}

bool Node::addPredecessor(Node *node) {
    if (!node) {
        return false;
    }
    predecessors_.insert(node);
    return node->successors_.insert(this).second;
}

bool Node::addSuccessor(Node *node) {
    if (!node) {
        return false;
    }
    successors_.insert(node);
    return node->predecessors_.insert(this).second;
}

bool Node::removePredecessor(Node *node) {
    if (!node) {
        return false;
    }
    predecessors_.erase(node);
    return node->successors_.erase(this);
}

bool Node::removeSuccessor(Node *node) {
    if (!node) {
        return false;
    }
    successors_.erase(node);
    return node->predecessors_.erase(this);
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

bool Node::isLock() const {
    return false;
}

bool Node::isUnlock() const {
    return false;
}

void Node::printOutcomingEdges(ostream &ostream) const {
    for (const auto &successor : successors_) {
        ostream << this->dotName() << " -> " << successor->dotName() << "\n";
    }
}

void Node::setThreadRegion(ThreadRegion *threadRegion) {
    if (threadRegion == nullptr) {
        return;
    }
    threadRegion_ = threadRegion;
    threadRegion_->insertNode(this);
}

ThreadRegion *Node::threadRegion() const {
    return threadRegion_;
}

ControlFlowGraph *Node::controlFlowGraph() {
    return controlFlowGraph_;
}

void Node::dfsComputeThreadRegions() {
    computeThreadRegionsOnSuccessorsFromNode(successors(), this);
}

void Node::dfsComputeCriticalSections(LockNode * lock) {
    computeCriticalSectionsDependentOnLock(successors(), lock);
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

bool shouldFinish(LockNode * lock, Node * successor) {
    if (successor->isUnlock()) {
        UnlockNode * unlock = static_cast<UnlockNode *>(successor);
        auto iterator = lock->correspondingUnlocks().find(unlock);
        if (iterator != lock->correspondingUnlocks().end()) {
            return true;
        }
    }
    return false;
}
