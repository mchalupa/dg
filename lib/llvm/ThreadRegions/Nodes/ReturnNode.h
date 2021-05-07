#ifndef RETURNNODE_H
#define RETURNNODE_H

#include "Node.h"

class ReturnNode : public Node {
  public:
    ReturnNode(const llvm::Instruction *instruction = nullptr);
};

#endif // RETURNNODE_H
