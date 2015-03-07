/// XXX add licence
//

#ifndef _DEPENDENCE_GRAPH_H_
#define _DEPENDENCE_GRAPH_H_

#include <utility>
#include <set>

#include "DGUtil.h"

namespace dg {

class DependenceGraph;

// one node in DependenceGraph
class DGNode
{
public:
    // TODO when LLVM is enabled, use SmallPtrSet
    // else BDD ?
    typedef std::set<DGNode *> ControlEdgesType;
    typedef std::set<DGNode *> DependenceEdgesType;

    typedef ControlEdgesType::iterator control_iterator;
    typedef ControlEdgesType::const_iterator const_control_iterator;
    typedef DependenceEdgesType::iterator dependence_iterator;
    typedef DependenceEdgesType::const_iterator const_dependence_iterator;

    DGNode();

    bool addControlEdge(DGNode *n);
    bool addDependenceEdge(DGNode *n);
    DependenceGraph *addSubgraph(DependenceGraph *);
    DependenceGraph *addParameters(DependenceGraph *);

    void dump(void) const;

    control_iterator control_begin(void) { return controlEdges.begin(); }
    const_control_iterator control_begin(void) const { return controlEdges.begin(); }
    control_iterator control_end(void) { return controlEdges.end(); }
    const_control_iterator control_end(void) const { return controlEdges.end(); }

    dependence_iterator dependence_begin(void) { return dependenceEdges.begin(); }
    const_dependence_iterator dependence_begin(void) const { return dependenceEdges.begin(); }
    dependence_iterator dependence_end(void) { return dependenceEdges.end(); }
    const_dependence_iterator dependence_end(void) const { return dependenceEdges.end(); }

    DependenceGraph *getSubgraph(void) const { return subgraph; }
    DependenceGraph *getParameters(void) const { return parameters; }

    std::pair<DependenceGraph *,
              DependenceGraph *> getSubgraphWithParams(void) const;

private:
    ControlEdgesType controlEdges;
    DependenceEdgesType dependenceEdges;
    
    // Nodes that have control/dep edge to this node
    ControlEdgesType revControlEdges;
    DependenceEdgesType revDependenceEdges;
    
    DependenceGraph *subgraph;
    // instead of adding parameter in/out nodes to parent
    // graph, we create new small graph just with these
    // nodes and summary edges (as dependence edges)
    DependenceGraph *parameters;
};

// DependenceGraph
class DependenceGraph
{
public:
    DependenceGraph();

    typedef std::set<DGNode *> ContainerType;
    typedef ContainerType::iterator iterator;
    typedef ContainerType::const_iterator const_iterator;

    DGNode *getEntry(void) const { return entryNode; }
    DGNode *setEntry(DGNode *n);
    bool addNode(DGNode *n);
    DGNode *removeNode(DGNode *n);

    void dump(void) const;
    const unsigned int getNodesNum(void) const { return nodes_num; }

    iterator begin(void) { return nodes.begin(); }
    const_iterator begin(void) const { return nodes.begin(); }
    iterator end(void) { return nodes.end(); }
    const_iterator end(void) const { return nodes.end(); }
private:
    DGNode *entryNode;
    ContainerType nodes;
    unsigned int nodes_num;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
