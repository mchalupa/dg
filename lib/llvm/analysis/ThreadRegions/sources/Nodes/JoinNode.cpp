#include "JoinNode.h"
#include "ExitNode.h"
#include "ForkNode.h"

JoinNode::JoinNode(ControlFlowGraph *controlFlowGraph,
                   const llvm::Value *value):LlvmNode(controlFlowGraph, value){}

void JoinNode::addCorrespondingFork(ForkNode *forkNode) {
    correspondingForks_.insert(forkNode);
    forkNode->correspondingJoins_.insert(this);
}

void JoinNode::addJoinPredecessor(ExitNode *exitNode) {
    joinPredecessors_.insert(exitNode);
    exitNode->joinSuccessors_.insert(this);
}

void JoinNode::removeJoinPredecessor(ExitNode *exitNode) {
    joinPredecessors_.erase(exitNode);
    exitNode->joinSuccessors_.erase(this);
}

const std::set<const ExitNode *> &JoinNode::joinPredecessors() const {
    return joinPredecessors_;
}

std::set<ForkNode *> JoinNode::correspondingForks() const {
    return correspondingForks_;
}


bool JoinNode::isJoin() const {
    return true;
}
