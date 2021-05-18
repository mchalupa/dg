#ifndef THREADREGION_H
#define THREADREGION_H

#include <iosfwd>
#include <set>

class Node;
class ControlFlowGraph;

namespace llvm {
class Instruction;
}

class ThreadRegion {
    int id_;
    Node *foundingNode_;
    std::set<Node *> nodes_;
    std::set<ThreadRegion *> predecessors_;
    std::set<ThreadRegion *> successors_;

    static int lastId;

  public:
    ThreadRegion(Node *node);

    int id() const;

    bool addPredecessor(ThreadRegion *predecessor);
    bool addSuccessor(ThreadRegion *threadRegion);

    bool removePredecessor(ThreadRegion *predecessor);
    bool removeSuccessor(ThreadRegion *successor);

    const std::set<ThreadRegion *> &predecessors() const;
    std::set<ThreadRegion *> predecessors();

    const std::set<ThreadRegion *> &successors() const;
    std::set<ThreadRegion *> successors();

    bool insertNode(Node *node);
    bool removeNode(Node *node);

    Node *foundingNode() const;

    const std::set<Node *> &nodes() const;
    std::set<Node *> nodes();

    void printNodes(std::ostream &ostream);
    void printEdges(std::ostream &ostream);

    std::string dotName();
    /**
     * @brief returns all llvm instructions contained in the thread region
     * @return
     */
    std::set<const llvm::Instruction *> llvmInstructions() const;
};

#endif // THREADREGION_H
