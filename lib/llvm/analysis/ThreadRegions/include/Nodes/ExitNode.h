#ifndef EXITNODE_H
#define EXITNODE_H

#include "ArtificialNode.h"

#include <set>

class JoinNode;

class ExitNode : public ArtificialNode
{
private:
    std::set<JoinNode *> joinSuccessors_;
public:
    ExitNode(ControlFlowGraph * controlFlowGraph);

    void addJoinSuccessor(JoinNode *joinNode);
    void removeJoinSuccessor(JoinNode *joinNode);

    const std::set<JoinNode *> & joinSuccessors() const;

    void printOutcomingEdges(std::ostream &ostream) const override;

    bool isExit() const override;

    void dfsComputeThreadRegions() override;

    void dfsComputeCriticalSections(LockNode * lock) override;

    friend class JoinNode;
};

#endif // EXITNODE_H
