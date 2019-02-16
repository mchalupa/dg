#include "JoinNode.h"
#include "ExitNode.h"
#include "ForkNode.h"

#include "llvm/IR/Instructions.h"

JoinNode::JoinNode(const llvm::Instruction *value):Node(NodeType::JOIN, value){}

bool JoinNode::addCorrespondingFork(ForkNode *forkNode) {
    if (!forkNode) {
        return false;
    }
    correspondingForks_.insert(forkNode);
    return forkNode->correspondingJoins_.insert(this).second;
}

bool JoinNode::addJoinPredecessor(ExitNode *exitNode) {
    if (!exitNode) {
        return false;
    }
    joinPredecessors_.insert(exitNode);
    return exitNode->joinSuccessors_.insert(this).second;
}

bool JoinNode::removeJoinPredecessor(ExitNode *exitNode) {
    if (!exitNode) {
        return false;
    }
    joinPredecessors_.erase(exitNode);
    return exitNode->joinSuccessors_.erase(this);
}

const std::set<const ExitNode *> &JoinNode::joinPredecessors() const {
    return joinPredecessors_;
}

std::set<const ExitNode *> JoinNode::joinPredecessors() {
    return joinPredecessors_;
}

std::size_t JoinNode::predecessorsNumber() const {
    return predecessors().size() + joinPredecessors_.size();
}

const std::set<ForkNode *> &JoinNode::correspondingForks() const {
    return correspondingForks_;
}

std::set<ForkNode *> JoinNode::correspondingForks() {
    return correspondingForks_;
}
