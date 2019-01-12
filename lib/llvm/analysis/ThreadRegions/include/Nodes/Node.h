#ifndef NODE_H
#define NODE_H

#include <string>
#include <set>
#include <ostream>

#include "dg/llvm/analysis/ThreadRegions/DfsState.h"
#include "dg/llvm/analysis/ThreadRegions/ThreadRegion.h"

class ControlFlowGraph;

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

    void setName(std::string &name);

    const std::string & name() const;
          std::string   name();

    std::string dotName() const;

    void addPredecessor(Node *);
    void addSuccessor(Node *);

    void removePredecessor(Node *);
    void removeSuccessor(Node *);

    const std::set<Node *> & predecessors() const;
    const std::set<Node *> & successors() const;

    virtual bool isArtificial() const;

    virtual bool isJoin() const;

    virtual bool isEntry() const;

    virtual bool isEndIf() const;

    virtual bool isFork() const;

    virtual bool isExit() const;

    virtual std::string dump() const = 0;

    virtual void printOutcomingEdges(std::ostream & ostream) const;

    void setThreadRegion(ThreadRegion *threadRegion);

    ThreadRegion * threadRegion() const;

    ControlFlowGraph * controlFlowGraph();

    virtual void dfsVisit();

    void setDfsState(DfsState state);

    DfsState dfsState();
};

bool shouldCreateNewRegion(const Node * caller,
                           const Node * successor);

template<typename T>
void visitSuccessorsFromNode(const std::set<T*> &successors, Node *caller) {
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
            successor->dfsVisit();
            successor->setDfsState(DfsState::EXAMINED);
            if (createdNewRegion) {
                successor->threadRegion()->setDfsState(DfsState::EXAMINED);
            }
        }
    }
}

#endif // NODE_H
