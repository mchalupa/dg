#include <llvm/IR/CFG.h>

#include "FunctionGraph.h"
#include "EntryNode.h"
#include "ExitNode.h"

using namespace llvm;

FunctionGraph::FunctionGraph(const Function *llvmFunction, ControlFlowGraph *controlFlowGraph)
    :controlFlowGraph(controlFlowGraph),
     llvmFunction_(llvmFunction),
     entryNode_(new EntryNode(controlFlowGraph)),
     exitNode_(new ExitNode(controlFlowGraph)) {}

FunctionGraph::~FunctionGraph() {
    for (auto it : llvmToBlockGraphMap) {
        delete it.second;
    }
}

EntryNode *FunctionGraph::entryNode() const {
    return entryNode_.get();
}

ExitNode *FunctionGraph::exitNode() const {
    return exitNode_.get();
}

const llvm::Function *FunctionGraph::llvmFunction() const {
    return llvmFunction_;
}

BlockGraph *FunctionGraph::findBlock(const BasicBlock *llvmBlock) const {
    auto iterator = llvmToBlockGraphMap.find(llvmBlock);
    if (iterator == llvmToBlockGraphMap.end()) {
        return nullptr;
    } else {
        return iterator->second;
    }
}

Node * FunctionGraph::findNode (const llvm::Value * value) const {
    const llvm::Instruction *inst = dyn_cast<llvm::Instruction>(value);
    auto block = findBlock(inst->getParent());
    auto node = block->findNode(value);
    return node;
}

void FunctionGraph::clearDfsState() {
    for (auto keyValue : llvmToBlockGraphMap) {
        keyValue.second->clearDfsState();
    }
}

void FunctionGraph::build() {
    if (llvmFunction_->size() == 0) {
        entryNode_->addSuccessor(exitNode_.get());
        return;
    }

    for (const BasicBlock &basicBlock : *llvmFunction_) {
        auto iteratorAndBool = llvmToBlockGraphMap.emplace(&basicBlock, new BlockGraph(&basicBlock, controlFlowGraph));
        BlockGraph *blockGraph = iteratorAndBool.first->second;
        blockGraph->build();
    }

    auto iterator = llvmToBlockGraphMap.find(&llvmFunction_->getEntryBlock());
    BlockGraph *blockGraph = iterator->second;
    entryNode_->addSuccessor(blockGraph->firstNode());

    for (const BasicBlock &basicBlock : *llvmFunction_) {
        iterator = llvmToBlockGraphMap.find(&basicBlock);
        blockGraph = iterator->second;
        for (auto it = succ_begin(&basicBlock); it != succ_end(&basicBlock); ++it) {
            auto successorIterator = llvmToBlockGraphMap.find(*it);
            auto successorBlockGraph = successorIterator->second;
            blockGraph->lastNode()->addSuccessor(successorBlockGraph->firstNode());
        }
        if (blockGraph->containsReturn()) {
            blockGraph->lastNode()->addSuccessor(exitNode_.get());
        }
    }
}

void FunctionGraph::printNodes(std::ostream &ostream) const {
    ostream << entryNode_->dump();
    for (const auto &iterator : llvmToBlockGraphMap) {
        iterator.second->printNodes(ostream);
    }
    ostream << exitNode_->dump();
}

void FunctionGraph::printEdges(std::ostream &ostream) const {
    entryNode_->printOutcomingEdges(ostream);
    for (const auto &iterator : llvmToBlockGraphMap) {
        iterator.second->printEdges(ostream);
    }
    exitNode_->printOutcomingEdges(ostream);
}

