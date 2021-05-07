#ifndef BLOCKGRAPH_H
#define BLOCKGRAPH_H

#include <map>
#include <ostream>
#include <set>

namespace llvm {
class BasicBlock;
}

class Node;

class BlockGraph {
  private:
    const llvm::BasicBlock *llvmBlock_ = nullptr;

    Node *firstNode_ = nullptr;
    Node *lastNode_ = nullptr;

  public:
    BlockGraph(const llvm::BasicBlock *llvmBlock, Node *firstNode,
               Node *lastNode);

    BlockGraph(const BlockGraph &) = delete;

    BlockGraph &operator==(const BlockGraph &) = delete;

    const llvm::BasicBlock *llvmBlock() const;

    Node *firstNode() const;
    Node *lastNode() const;
};

#endif // BLOCKGRAPH_H
