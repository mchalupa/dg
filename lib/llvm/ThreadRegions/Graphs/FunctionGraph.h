#ifndef FUNCTIONGRAPH_H
#define FUNCTIONGRAPH_H

namespace llvm {
class Function;
}

class EntryNode;
class ExitNode;

class FunctionGraph {
  private:
    const llvm::Function *llvmFunction_ = nullptr;

    EntryNode *entryNode_ = nullptr;
    ExitNode *exitNode_ = nullptr;

  public:
    FunctionGraph(const llvm::Function *llvmFunction, EntryNode *entryNode,
                  ExitNode *exitNode);

    FunctionGraph(const FunctionGraph &) = delete;

    FunctionGraph &operator==(const FunctionGraph &) = delete;

    const llvm::Function *llvmFunction() const;

    EntryNode *entryNode() const;
    ExitNode *exitNode() const;
};

#endif // FUNCTIONGRAPH_H
