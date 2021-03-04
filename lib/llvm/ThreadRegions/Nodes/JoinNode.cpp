#include "JoinNode.h"
#include "ExitNode.h"
#include "ForkNode.h"

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/Instructions.h>
SILENCE_LLVM_WARNINGS_POP

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
