#include "JoinNode.h"
#include "ExitNode.h"
#include "ForkNode.h"

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Instructions.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

JoinNode::JoinNode(const llvm::Instruction *value, const llvm::CallInst *callInst)
    :Node(NodeType::JOIN, value, callInst){}

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
