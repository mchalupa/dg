#ifndef ENTRYNODE_H
#define ENTRYNODE_H

#include "ArtificialNode.h"

#include <set>

class ForkNode;

class EntryNode : public ArtificialNode
{
private:
    std::set<ForkNode *> forkPredecessors_;
public:
    EntryNode(ControlFlowGraph* controlFlowGraph);

    void addForkPredecessor(ForkNode *forkNode);

    void removeForkPredecessor(ForkNode *forkNode);

    const std::set<ForkNode *> & forkPredecessors() const;

    bool isEntry() const override;

    friend class ForkNode;
};

#endif // ENTRYNODE_H
