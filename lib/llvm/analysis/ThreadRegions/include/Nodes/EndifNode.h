#ifndef ENDIFNODE_H
#define ENDIFNODE_H

#include "ArtificialNode.h"

class EndifNode : public ArtificialNode
{
public:
    EndifNode(ControlFlowGraph * controlFlowGraph);

    bool isEndIf() const override;
};

#endif // ENDIFNODE_H
