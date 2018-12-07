#ifndef ARTIFICIALNODE_H
#define ARTIFICIALNODE_H

#include "Node.h"

class ArtificialNode : public Node
{
public:
    ArtificialNode(ControlFlowGraph * controlFlowGraph);

    bool isArtificial() const override;

    std::string dump() const override;
};

#endif // ARTIFICIALNODE_H
