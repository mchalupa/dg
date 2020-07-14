#include "BlockGraph.h"

BlockGraph::BlockGraph(const llvm::BasicBlock *llvmBlock,
                       Node *firstNode,
                       Node *lastNode):llvmBlock_(llvmBlock),
                                       firstNode_(firstNode),
                                       lastNode_(lastNode)

{}

Node *BlockGraph::firstNode() const {
    return firstNode_;
}

Node *BlockGraph::lastNode() const {
    return lastNode_;
}

const llvm::BasicBlock *BlockGraph::llvmBlock() const {
    return llvmBlock_;
}
