#ifndef FORKNODE_H
#define FORKNODE_H

#include "Node.h"

class EntryNode;
class JoinNode;

class ForkNode : public LlvmNode
{
    std::set<EntryNode *> forkSuccessors_;
    std::set<JoinNode *> correspondingJoins_;
public:
    ForkNode(ControlFlowGraph * controlFlowGraph, const llvm::Value * value);

    void addCorrespondingJoin(JoinNode * joinNode);

    void addForkSuccessor(EntryNode * entryNode);

    void removeForkSuccessor(EntryNode * entryNode);

    const std::set<EntryNode *> forkSuccessors() const;

    std::set<JoinNode *> correspondingJoins() const;

    void printOutcomingEdges(std::ostream &ostream) const override;

    bool isFork() const override;

    void dfsComputeThreadRegions() override;

    void dfsComputeCriticalSections(LockNode * lock) override;

    friend class EntryNode;
    friend class JoinNode;
};

#endif // FORKNODE_H
