#include "Node.h"

UnlockNode::UnlockNode(ControlFlowGraph * controlFlowGraph,
                       const llvm::Value * value):LlvmNode(controlFlowGraph, value)
{}

void UnlockNode::addCorrespondingLock(LockNode *lockNode) {
    correspondingLocks_.insert(lockNode);
    lockNode->correspondingUnlocks_.insert(this);
}

std::set<LockNode *> UnlockNode::correspondingLocks() const {
    return correspondingLocks_;
}

bool UnlockNode::isLock() const {
    return false;
}

bool UnlockNode::isUnlock() const {
    return true;
}
