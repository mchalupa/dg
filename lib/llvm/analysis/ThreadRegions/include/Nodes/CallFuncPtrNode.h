#ifndef CALLFUNCPTRNODE_H
#define CALLFUNCPTRNODE_H

#include "Node.h"

class CallFuncPtrNode : public Node
{
public:
    CallFuncPtrNode(const llvm::Instruction * instruction);
};

#endif // CALLFUNCPTRNODE_H
