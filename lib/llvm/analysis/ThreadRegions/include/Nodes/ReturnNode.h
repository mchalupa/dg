#ifndef RETURNNODE_H
#define RETURNNODE_H

#include "ArtificialNode.h"

class ReturnNode : public ArtificialNode
{
public:
    ReturnNode(ControlFlowGraph * controlFlowGraph);
};

#endif // RETURNNODE_H
