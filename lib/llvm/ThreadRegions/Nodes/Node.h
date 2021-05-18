#ifndef NODE_H
#define NODE_H

#include <iosfwd>
#include <set>
#include <string>

#include "NodeIterator.h"

namespace llvm {
class CallInst;
class Instruction;
} // namespace llvm

enum class NodeType {
    GENERAL,
    FORK,
    JOIN,
    LOCK,
    UNLOCK,
    ENTRY,
    EXIT,
    CALL,
    CALL_FUNCPTR,
    CALL_RETURN,
    RETURN
};

inline std::string nodeTypeToString(enum NodeType type) {
#define ELEM(t)                                                                \
    case t:                                                                    \
        do {                                                                   \
            return (#t);                                                       \
        } while (0);                                                           \
        break;
    switch (type) {
        ELEM(NodeType::GENERAL)
        ELEM(NodeType::FORK)
        ELEM(NodeType::JOIN)
        ELEM(NodeType::LOCK)
        ELEM(NodeType::UNLOCK)
        ELEM(NodeType::ENTRY)
        ELEM(NodeType::EXIT)
        ELEM(NodeType::CALL)
        ELEM(NodeType::CALL_RETURN)
        ELEM(NodeType::CALL_FUNCPTR)
        ELEM(NodeType::RETURN)
    };
#undef ELEM
    return "undefined";
}

class Node {
  private:
    const int id_;
    const NodeType nodeType_;
    const llvm::Instruction *llvmInstruction_;
    const llvm::CallInst *callInstruction_;
    std::set<Node *> predecessors_;
    std::set<Node *> successors_;

    static int lastId;

  public:
    Node(NodeType type, const llvm::Instruction *instruction = nullptr,
         const llvm::CallInst *callInst = nullptr);

    virtual ~Node() = default;

    NodeIterator begin() const;

    NodeIterator end() const;

    int id() const;

    NodeType getType() const;

    std::string dotName() const;

    bool addPredecessor(Node * /*node*/);
    bool addSuccessor(Node * /*node*/);

    bool removePredecessor(Node * /*node*/);
    bool removeSuccessor(Node * /*node*/);

    const std::set<Node *> &predecessors() const;
    const std::set<Node *> &successors() const;

    virtual std::size_t predecessorsNumber() const;
    virtual std::size_t successorsNumber() const;

    bool isArtificial() const;

    const llvm::Instruction *llvmInstruction() const;

    const llvm::CallInst *callInstruction() const;

    std::string dump() const;

    std::string label() const;

    virtual void printOutcomingEdges(std::ostream &ostream) const;
};

#endif // NODE_H
