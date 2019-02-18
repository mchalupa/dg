#ifndef LOCKNODE_H
#define LOCKNODE_H

#include "Node.h"

class UnlockNode;

class LockNode : public Node
{
    std::set<UnlockNode *>      correspondingUnlocks_;
public:
    LockNode(const llvm::Instruction * instruction = nullptr);

    bool addCorrespondingUnlock(UnlockNode * unlockNode);

    const std::set<UnlockNode *> & correspondingUnlocks() const;
          std::set<UnlockNode *>   correspondingUnlocks();
};

#endif // LOCKNODE_H
