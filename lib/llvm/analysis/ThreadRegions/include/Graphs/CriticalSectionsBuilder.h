#ifndef CRITICALSECTIONSBUILDER_H
#define CRITICALSECTIONSBUILDER_H

#include <vector>
#include <map>
#include <set>

class Node;
class LockNode;
class UnlockNode;
class ExitNode;
class ForkNode;

namespace llvm {
    class Instruction;
    class CallInst;
}

class CriticalSection {
private:
    LockNode *          lock_;
    std::set<Node *>    nodes_;

public:
    CriticalSection(LockNode * lock);

    const llvm::CallInst * lock() const;

    std::set<const llvm::Instruction *> nodes() const;

    std::set<const llvm::CallInst *> unlocks() const;

    bool insertNodes(const std::set<Node *> & nodes_);
};

class CriticalSectionsBuilder
{
    std::set<LockNode *>            locks_;

    LockNode *                      currentLock;
    std::set<UnlockNode *>          currentUnlocks;

    std::set<Node *>                visited_;
    std::set<Node *>                examined_;

    std::map<const llvm::CallInst *,CriticalSection *>     criticalSections_;
public:
    CriticalSectionsBuilder();

    ~CriticalSectionsBuilder();

    bool buildCriticalSection(LockNode * lock);

    std::set<const llvm::CallInst *> locks() const;

    std::set<const llvm::Instruction *> correspondingNodes(const llvm::CallInst * lock) const;

    std::set<const llvm::CallInst *> correspondingUnlocks(const llvm::CallInst * lock) const;

private:
    bool populateCriticalSection();

    void visitNode(Node * node);

    void preVisit(Node * node);

    void visit(Node * node);

    void postVisit(Node * node);

    bool visited(Node * node) const;

    bool examined(Node * node) const;
};

#endif // CRITICALSECTIONSBUILDER_H
