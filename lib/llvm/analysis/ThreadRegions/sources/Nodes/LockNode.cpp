#include "Node.h"

LockNode::LockNode(ControlFlowGraph * controlFlowGraph, 
                   const llvm::Value * value):LlvmNode(controlFlowGraph, value) 
{}

void LockNode::addCorrespondingUnlock(UnlockNode * unlockNode) {
    correspondingUnlocks_.insert(unlockNode);
    unlockNode->correspondingLocks_.insert(this);
}

std::set<UnlockNode *> LockNode::correspondingUnlocks() const {
    return correspondingUnlocks_;
}

std::set<const llvm::Value *> LockNode::llvmUnlocks() const {
    std::set<const llvm::Value *> unlocks;
    for (auto unlock : correspondingUnlocks_) {
        unlocks.insert(unlock->llvmValue());
    }
    return unlocks;
}

bool LockNode::isLock() const {
    return true;
}

bool LockNode::isUnlock() const {
    return false;
}

bool LockNode::addToCriticalSection(Node *node) {
    return criticalSection_.insert(node).second;
}

std::set<Node *> LockNode::criticalSection() {
    return criticalSection_;
}

std::set<const llvm::Value *> LockNode::llvmCriticalSectioon() {
    std::set<const llvm::Value *> values;
    for (auto node : criticalSection_) {
        if (!node->isArtificial()) {
            auto llvmNode = static_cast<LlvmNode *>(node);
            values.insert(llvmNode->llvmValue());
        }
    }
    return values;
}
