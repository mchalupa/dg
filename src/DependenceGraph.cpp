// XXX license

#include <cassert>

#ifdef DEBUG
#include <cstdio>
#endif

#include "DependenceGraph.h"
#include "DGUtil.h"

namespace dg {

#ifdef DEBUG
void DGNode::dump(void) const
{
	if (subgraph)
		fprintf(stderr, "[%p] CALL to [%p]\n", this, subgraph);
	else
		fprintf(stderr, "[%p]\n", this);

	for ( DGNode *n : controlEdges )
		fprintf(stderr, "\tC: [%p]\n", n);

	for ( DGNode *n : refControlEdges )
		fprintf(stderr, "\trC: [%p]\n", n);

	for ( DGNode *n : dependenceEdges )
		fprintf(stderr, "\tD: [%p]\n", n);

	for ( DGNode *n : refDependenceEdges )
		fprintf(stderr, "\trD: [%p]\n", n);
}

void DependenceGraph::dump(void) const
{
	for ( auto n : nodes )
		n->dump();
}
#endif // DEBUG

std::pair<DependenceGraph *, DependenceGraph *>
DGNode::getSubgraphWithParams(void) const
{
	return std::pair<DependenceGraph *,
					 DependenceGraph *>	(subgraph, parameters);
}

bool DGNode::addControlEdge(DGNode *n)
{
#ifdef DEBUG
    bool ret1, ret2;

    ret1 = n->refControlEdges.insert(this).second;
    ret2 = n->controlEdges.insert(n).second;

    // make sure we don't have this edge yet
    assert(ret1 == ret2);

    DBG(CONTROL, "Added control edge [%p]->[%p]\n", this, n);

    return ret2;
#else
    n->refControlEdges.insert(this);
    return n->controlEdges.insert(n).second;
#endif
}

bool DGNode::addDependenceEdge(DGNode *n)
{
#ifdef DEBUG
    bool ret1, ret2;

    ret1 = n->refDependenceEdges.insert(this).second;
    ret2 = n->dependenceEdges.insert(n).second;

    assert(ret1 == ret2);

    DBG(DEPENDENCE, "Added dependence edge [%p]->[%p]\n", this, n);

    return ret2;
#else
    n->refDependenceEdges.insert(this);
    return n->dependenceEdges.insert(n).second;
#endif
}

} // namespace dg
