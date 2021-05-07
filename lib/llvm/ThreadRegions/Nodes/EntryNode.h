#ifndef ENTRYNODE_H
#define ENTRYNODE_H

#include "Node.h"

#include <set>

class ForkNode;

class EntryNode : public Node {
  private:
    std::set<ForkNode *> forkPredecessors_;

  public:
    EntryNode();

    bool addForkPredecessor(ForkNode *forkNode);

    bool removeForkPredecessor(ForkNode *forkNode);

    const std::set<ForkNode *> &forkPredecessors() const;
    std::set<ForkNode *> forkPredecessors();

    std::size_t predecessorsNumber() const override;

    friend class ForkNode;
};

#endif // ENTRYNODE_H
