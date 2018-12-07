#ifndef FUNCTIONGRAPH_H
#define FUNCTIONGRAPH_H

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>

#include "BlockGraph.h"

#include <map>
#include <memory>
#include <ostream>

class EntryNode;
class ExitNode;
class ControlFlowGraph;

class FunctionGraph
{
private:
    ControlFlowGraph * controlFlowGraph;

    const llvm::Function * llvmFunction_ = nullptr;

    std::unique_ptr<EntryNode> entryNode_;
    std::unique_ptr<ExitNode> exitNode_;

    std::map<const llvm::BasicBlock *, BlockGraph *> llvmToBlockGraphMap;

public:
    FunctionGraph(const llvm::Function *llvmFunction, ControlFlowGraph *controlFlowGraph);

    FunctionGraph(const FunctionGraph &) = delete;

    FunctionGraph & operator==(const FunctionGraph &) = delete;

    ~FunctionGraph();

    EntryNode * entryNode() const;
    ExitNode * exitNode() const;

    const llvm::Function *llvmFunction() const;

    BlockGraph * findBlock(const llvm::BasicBlock * llvmBlock) const;

    void build();

    void printNodes(std::ostream & ostream) const;

    void printEdges(std::ostream & ostream) const;
};

#endif // FUNCTIONGRAPH_H
