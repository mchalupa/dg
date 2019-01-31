#ifndef NODE_H
#define NODE_H

#include <string>
#include <set>
#include <ostream>

#include "dg/llvm/analysis/ThreadRegions/DfsState.h"
#include "dg/llvm/analysis/ThreadRegions/ThreadRegion.h"

class ControlFlowGraph;
class LockNode;
class UnlockNode;

class Node
{
private:
    int                     id_;
    std::string             name_ = "";
    std::set<Node *>        predecessors_;
    std::set<Node *>        successors_;
    ControlFlowGraph *      controlFlowGraph_ = nullptr;
    ThreadRegion *          threadRegion_ = nullptr;
    DfsState                dfsState_ = DfsState::UNDISCOVERED;

    static int lastId;

public:
    Node(ControlFlowGraph * controlFlowGraph);

    virtual ~Node() = default;

    int id() const;

    void setName(const std::string &name);

    const std::string & name() const;
          std::string   name();

    std::string dotName() const;

    bool addPredecessor(Node *);
    bool addSuccessor(Node *);

    bool removePredecessor(Node *);
    bool removeSuccessor(Node *);

    const std::set<Node *> & predecessors() const;
    const std::set<Node *> & successors() const;

    virtual bool isArtificial() const;

    virtual bool isJoin() const;

    virtual bool isEntry() const;

    virtual bool isEndIf() const;

    virtual bool isFork() const;

    virtual bool isExit() const;

    virtual bool isLock() const;

    virtual bool isUnlock() const;

    virtual std::string dump() const = 0;

    virtual void printOutcomingEdges(std::ostream & ostream) const;

    void setThreadRegion(ThreadRegion *threadRegion);

    ThreadRegion * threadRegion() const;

    ControlFlowGraph * controlFlowGraph();

    virtual void dfsComputeThreadRegions();

    virtual void dfsComputeCriticalSections(LockNode * lock);

    void setDfsState(DfsState state);

    DfsState dfsState();
};

namespace llvm {
    class Value;
}

class LlvmNode : public Node
{
    const llvm::Value * llvmValue_ = nullptr;
public:
    LlvmNode(ControlFlowGraph * controlFlowGraph, const llvm::Value * value);

    std::string dump() const override;

    const llvm::Value * llvmValue() const;
};

class LockNode : public LlvmNode
{
    std::set<UnlockNode *> correspondingUnlocks_;
    std::set<Node *>       criticalSection_;

public:
    LockNode(ControlFlowGraph * controlFlowGraph, const llvm::Value * value);

    void addCorrespondingUnlock(UnlockNode * unlockNode);

    std::set<UnlockNode *> correspondingUnlocks() const;

    std::set<const llvm::Value *> llvmUnlocks() const;

    bool isLock() const override;

    bool isUnlock() const override;

    bool addToCriticalSection(Node * node);

    std::set<Node *> criticalSection();

    std::set<const llvm::Value *> llvmCriticalSectioon();

    friend class UnlockNode;
};

class UnlockNode : public LlvmNode
{
    std::set<LockNode *> correspondingLocks_;

public:
    UnlockNode(ControlFlowGraph * controlFlowGraph, const llvm::Value * value);

    void addCorrespondingLock(LockNode * lockNode);

    std::set<LockNode *> correspondingLocks() const;

    bool isLock() const override;

    bool isUnlock() const override;

    friend class LockNode;
};

bool shouldCreateNewRegion(const Node * caller,
                           const Node * successor);

bool shouldFinish(LockNode * lock, Node * successor);

template<typename T>
void computeThreadRegionsOnSuccessorsFromNode(const std::set<T*> &successors, Node *caller) {
    for (const auto successor : successors) {
        if (successor->dfsState() == DfsState::DISCOVERED) {
            continue;
        } else if (successor->dfsState() == DfsState::EXAMINED) {
            if (successor->threadRegion()->dfsState() == DfsState::EXAMINED) {
                caller->threadRegion()->addSuccessor(successor->threadRegion());
            }
        } else {
            bool createdNewRegion = false;
            successor->setDfsState(DfsState::DISCOVERED);
            if (shouldCreateNewRegion(caller, successor)) {
                createdNewRegion = true;
                ThreadRegion * region = new ThreadRegion(caller->threadRegion()->controlFlowGraph());
                successor->setThreadRegion(region);
                caller->threadRegion()->addSuccessor(region);
            } else {
                successor->setThreadRegion(caller->threadRegion());
            }
            successor->dfsComputeThreadRegions();
            successor->setDfsState(DfsState::EXAMINED);
            if (createdNewRegion) {
                successor->threadRegion()->setDfsState(DfsState::EXAMINED);
            }
        }
    }
}

template<typename T>
void computeCriticalSectionsDependentOnLock(const std::set<T*> & successors, LockNode * lock) {
    for (const auto successor : successors) {
        if (successor->dfsState() != DfsState::UNDISCOVERED) {
            continue;
        } else {
            successor->setDfsState(DfsState::DISCOVERED);
            if (!shouldFinish(lock, successor)) {
                lock->addToCriticalSection(successor);
                successor->dfsComputeCriticalSections(lock);
            }
            successor->setDfsState(DfsState::EXAMINED);
        }
    }
}


#endif // NODE_H
