#include "ForkNode.h"
#include "EntryNode.h"
#include "JoinNode.h"
#include "ControlFlowGraph.h"

#include <iostream>

using namespace std;

ForkNode::ForkNode(ControlFlowGraph *controlFlowGraph, const llvm::Value *value):LlvmNode(controlFlowGraph, value) {}

void ForkNode::addCorrespondingJoin(JoinNode *joinNode){
    correspondingJoins_.insert(joinNode);
    joinNode->correspondingForks_.insert(this);
}

void ForkNode::addForkSuccessor(EntryNode *entryNode) {
    forkSuccessors_.insert(entryNode);
    entryNode->forkPredecessors_.insert(this);
}

void ForkNode::removeForkSuccessor(EntryNode *entryNode) {
    forkSuccessors_.erase(entryNode);
    entryNode->forkPredecessors_.erase(this);
}

const std::set<EntryNode *> ForkNode::forkSuccessors() const {
    return forkSuccessors_;
}

std::set<JoinNode *> ForkNode::correspondingJoins() const {
    return correspondingJoins_;
}


void ForkNode::printOutcomingEdges(ostream &ostream) const {
    Node::printOutcomingEdges(ostream);
    for (const auto &forkSuccessor : forkSuccessors_) {
        ostream << this->dotName() << " -> " << forkSuccessor->dotName() << " [style=dashed]\n";
    }
}


void ForkNode::dfsVisit(){
    visitSuccessorsFromNode(forkSuccessors(), this);
    visitSuccessorsFromNode(successors(), this);
}


bool ForkNode::isFork() const {
    return true;
}
