#ifndef FORKNODE_H
#define FORKNODE_H

#include "Node.h"

class EntryNode;
class JoinNode;

class ForkNode : public Node {
    std::set<EntryNode *> forkSuccessors_;
    std::set<JoinNode *> correspondingJoins_;

  public:
    ForkNode(const llvm::Instruction *instruction = nullptr,
             const llvm::CallInst *callInst = nullptr);

    bool addCorrespondingJoin(JoinNode *joinNode);

    bool addForkSuccessor(EntryNode *entryNode);

    bool removeForkSuccessor(EntryNode *entryNode);

    const std::set<EntryNode *> &forkSuccessors() const;
    std::set<EntryNode *> forkSuccessors();

    std::size_t successorsNumber() const override;

    const std::set<JoinNode *> &correspondingJoins() const;
    std::set<JoinNode *> correspondingJoins();

    void printOutcomingEdges(std::ostream &ostream) const override;

    friend class EntryNode;
    friend class JoinNode;
};

#endif // FORKNODE_H
