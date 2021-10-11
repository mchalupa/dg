#ifndef THREADREGIONBUILDER_H
#define THREADREGIONBUILDER_H

#include <iosfwd>
#include <set>
#include <unordered_map>

#include "dg/llvm/ThreadRegions/ThreadRegion.h"

class Node;
class ForkNode;
class ExitNode;

class ThreadRegionsBuilder {
  private:
    std::unordered_map<Node *, ThreadRegion *> visitedNodeToRegionMap;
    std::unordered_map<Node *, ThreadRegion *> examinedNodeToRegionMap;

    std::set<ThreadRegion *> threadRegions_;

  public:
    ThreadRegionsBuilder(std::size_t size = 0);

    ~ThreadRegionsBuilder();

    void build(Node *node);

    ThreadRegion *region(Node *node) const;

    void printNodes(std::ostream &ostream) const;

    void printEdges(std::ostream &ostream) const;

    void reserve(std::size_t size);

    void clear();

    std::set<ThreadRegion *> threadRegions();

  private:
    void visit(Node *node);

    bool unvisited(Node *node) const;

    bool visited(Node *node) const;

    bool examined(Node *node) const;

    bool examined(ThreadRegion *region) const;

    void populateThreadRegions();

    void clearComputingData();

    ThreadRegion *regionOfVisitedNode(Node *node) const;

    ThreadRegion *regionOfExaminedNode(Node *node) const;

    bool shouldCreateNewRegion(Node *caller, Node *successor) const;
};

#endif // THREADREGIONBUILDER_H
