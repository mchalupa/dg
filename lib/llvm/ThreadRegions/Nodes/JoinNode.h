#ifndef JOINNODE_H
#define JOINNODE_H

#include "Node.h"

#include <set>

class ExitNode;
class ForkNode;

class JoinNode : public Node {
  private:
    std::set<const ExitNode *> joinPredecessors_;
    std::set<ForkNode *> correspondingForks_;

  public:
    JoinNode(const llvm::Instruction *value = nullptr,
             const llvm::CallInst *callInst = nullptr);

    bool addCorrespondingFork(ForkNode *forkNode);

    bool addJoinPredecessor(ExitNode *exitNode);

    bool removeJoinPredecessor(ExitNode *exitNode);

    const std::set<const ExitNode *> &joinPredecessors() const;
    std::set<const ExitNode *> joinPredecessors();

    std::size_t predecessorsNumber() const override;

    const std::set<ForkNode *> &correspondingForks() const;
    std::set<ForkNode *> correspondingForks();

    friend class ExitNode;
    friend class ForkNode;
};

#endif // JOINNODE_H
