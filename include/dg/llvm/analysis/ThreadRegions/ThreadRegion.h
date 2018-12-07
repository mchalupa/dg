#ifndef THREADREGION_H
#define THREADREGION_H

#include <llvm/IR/Value.h>

#include <set>
#include <iosfwd>

#include "DfsState.h"

class Node;
class ControlFlowGraph;

class ThreadRegion
{
    int                         id_;
    std::set<Node *>            nodes_;
    std::set<ThreadRegion *>    predecessors_;
    std::set<ThreadRegion *>    successors_;
    ControlFlowGraph *          controlFlowGraph_ = nullptr;
    DfsState                    dfsState_                = DfsState::DISCOVERED;

    static int lastId;

public:
    ThreadRegion(ControlFlowGraph * controlFlowGraph);

    ControlFlowGraph * controlFlowGraph();

    void addPredecessor(ThreadRegion * predecessor);
    void addSuccessor(ThreadRegion * successor);

    void removePredecessor(ThreadRegion * predecessor);
    void removeSuccessor(ThreadRegion * successor);

    void insertNode(Node * node);
    void removeNode(Node * node);

    void printNodes(std::ostream & ostream);
    void printEdges(std::ostream & ostream);

    DfsState dfsState() const;

    void setDfsState(DfsState dfsState);

    std::string dotName();
    /**
     * @brief returns all llvm values contained in the thread region
     * @return
     */
    std::set<const llvm::Value *> llvmValues() const;
};

#endif // THREADREGION_H
