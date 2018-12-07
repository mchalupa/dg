#ifndef BLOCKGRAPH_H
#define BLOCKGRAPH_H

#include <llvm/IR/BasicBlock.h>

#include <map>
#include <set>
#include <ostream>

class Node;
class ArtificialNode;
class LlvmNode;
class ControlFlowGraph;

class BlockGraph
{
private:
    ControlFlowGraph * controlFlowGraph;

    const llvm::BasicBlock * llvmBlock_     = nullptr;
    Node * firstNode_                       = nullptr;
    Node * lastNode_                        = nullptr;
    bool   hasStructure_                    = false;

    std::set<Node *>                        allNodes;
    std::map<const llvm::Value *, Node *>   llvmToNodeMap;

public:
    BlockGraph(const llvm::BasicBlock *llvmBlock, ControlFlowGraph * controlFlowGraph);

    BlockGraph(const BlockGraph &) = delete;

    BlockGraph & operator==(const BlockGraph &) = delete;

    ~BlockGraph();

    Node * firstNode() const;
    Node * lastNode() const;

    const llvm::BasicBlock *llvmBlock() const;

    bool hasStructure() const;

    bool containsReturn() const;

    Node * findNode(const llvm::Value * value) const;

    void build();

    void printNodes(std::ostream & ostream) const;

    void printEdges(std::ostream & ostream) const;

private:
    int predecessorsNumber() const;

    void addNode(ArtificialNode * artificialNode);

    void addNode(LlvmNode * llvmNode);

    void buildCallInstruction(const llvm::CallInst * callInstruction,
                              Node *& lastConnectedNode);

};

#endif // BLOCKGRAPH_H
