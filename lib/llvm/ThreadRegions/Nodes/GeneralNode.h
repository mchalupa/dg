#ifndef GENERALNODE_H
#define GENERALNODE_H

#include "Node.h"

class GeneralNode : public Node
{
public:
    GeneralNode(const llvm::Instruction * instruction = nullptr);
};

#endif // GENERALNODE_H
