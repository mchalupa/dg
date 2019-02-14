#include "LockNode.h"

LockNode::LockNode(const llvm::Instruction * instruction):Node(NodeType::LOCK, instruction)
{}

bool LockNode::addCorrespondingUnlock(UnlockNode *unlockNode) {
    if (!unlockNode) {
        return false;
    }
    return correspondingUnlocks_.insert(unlockNode).second;
}

const std::set<UnlockNode *> &LockNode::correspondingUnlocks() const {
    return correspondingUnlocks_;
}

std::set<UnlockNode *> LockNode::correspondingUnlocks() {
    return correspondingUnlocks_;
}
