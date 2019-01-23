#ifndef JOINNODE_H
#define JOINNODE_H

#include "Node.h"

#include <set>

class ExitNode;
class ForkNode;

class JoinNode : public LlvmNode
{
private:
    std::set<const ExitNode *> joinPredecessors_;
    std::set<ForkNode *> correspondingForks_;
public:
    JoinNode(ControlFlowGraph * controlFlowGraph, const llvm::Value *value);

    void addCorrespondingFork(ForkNode * forkNode);

    void addJoinPredecessor(ExitNode *exitNode);

    void removeJoinPredecessor(ExitNode *exitNode);

    const std::set<const ExitNode *> & joinPredecessors() const;

    std::set<ForkNode *> correspondingForks() const;

    bool isJoin() const override;

    friend class ExitNode;
    friend class ForkNode;
};

#endif // JOINNODE_H
