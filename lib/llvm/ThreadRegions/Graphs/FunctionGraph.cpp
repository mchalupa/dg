#include "FunctionGraph.h"

FunctionGraph::FunctionGraph(const llvm::Function *llvmFunction,
                             EntryNode *entryNode, ExitNode *exitNode)
        : llvmFunction_(llvmFunction), entryNode_(entryNode),
          exitNode_(exitNode) {}

EntryNode *FunctionGraph::entryNode() const { return entryNode_; }

ExitNode *FunctionGraph::exitNode() const { return exitNode_; }

const llvm::Function *FunctionGraph::llvmFunction() const {
    return llvmFunction_;
}
