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

    bool addControlEdge(DGNode *n);
    bool addDependenceEdge(DGNode *n);

#ifdef DEBUG
    void dump(void) const;
#endif

    control_iterator control_begin(void) { return controlEdges.begin(); }
    const_control_iterator control_begin(void) const { return controlEdges.begin(); }
    control_iterator control_end(void) { return controlEdges.end(); }
    const_control_iterator control_end(void) const { return controlEdges.end(); }

    dependence_iterator dependence_begin(void) { return dependenceEdges.begin(); }
    const_dependence_iterator dependence_begin(void) const { return dependenceEdges.begin(); }
    dependence_iterator dependence_end(void) { return dependenceEdges.end(); }
    const_dependence_iterator dependence_end(void) const { return dependenceEdges.end(); }

    DependenceGraph *getSubgraph(void) const { return subgraph; }

	std::pair<DependenceGraph *,
			  DependenceGraph *> getSubgraphWithParams(void) const;

private:
    ControlEdgesType controlEdges;
    DependenceEdgesType dependenceEdges;
    
    // Nodes that have control/dep edge to this node
    ControlEdgesType refControlEdges;
    DependenceEdgesType refDependenceEdges;
    
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
    DependenceGraph()
    {
#ifdef DEBUG
    debug::init();
#endif
    }

    typedef std::set<DGNode *> ContainerType;
    typedef ContainerType::iterator iterator;
    typedef ContainerType::const_iterator const_iterator;

    DGNode *getEntry(void) const { return entryNode; }
    DGNode *addNode(DGNode *n) { nodes.insert(n); return n; }

#ifdef DEBUG
    void dump(void) const;
#endif

    iterator begin(void) { return nodes.begin(); }
    const_iterator begin(void) const { return nodes.begin(); }
    iterator end(void) { return nodes.end(); }
    const_iterator end(void) const { return nodes.end(); }
private:
    DGNode *entryNode;
    ContainerType nodes;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
