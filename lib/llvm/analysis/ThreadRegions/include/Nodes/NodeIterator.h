#ifndef NODEITERATOR_H
#define NODEITERATOR_H

#include <set>

class Node;
class ForkNode;
class JoinNode;
class EntryNode;
class ExitNode;

class NodeIterator {

private:
    const ForkNode * forkNode           = nullptr;
    const ExitNode * exitNode           = nullptr;
    std::set<Node *>::iterator          successorsIterator;
    std::set<EntryNode *>::iterator     forkSuccessorsIterator;
    std::set<JoinNode *>::iterator      joinSuccessorsIterator;

public:
    explicit NodeIterator(const Node * node = nullptr, bool begin = true);

    NodeIterator & operator++();
    NodeIterator   operator++(int);
    bool operator==(const NodeIterator & other) const;
    bool operator!=(const NodeIterator & other) const;
    Node * operator*() const;
};

#endif // NODEITERATOR_H
