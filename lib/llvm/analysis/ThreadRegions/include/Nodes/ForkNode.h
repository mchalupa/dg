#ifndef FORKNODE_H
#define FORKNODE_H

#include "LlvmNode.h"

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

    void dfsVisit() override;

    friend class EntryNode;
    friend class JoinNode;
};

#endif // FORKNODE_H
