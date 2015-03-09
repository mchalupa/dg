// XXX license

#include <cassert>
#include <cstdio>

#include "DependenceGraph.h"
#include "DGUtil.h"

namespace dg {

// --------------------------------------------------------
// --- Dumping/printing graph
// --------------------------------------------------------
void DGNode::dump(void) const
{
    if (subgraph) {
        fprintf(stderr, "[%p] CALL to [%p]\n", this, subgraph);

        if (parameters)
            parameters->dump();

    } else
        fprintf(stderr, "[%p]\n", this);

    for ( DGNode *n : controlEdges )
        fprintf(stderr, "\tC: [%p]\n", n);

    for ( DGNode *n : revControlEdges )
        fprintf(stderr, "\trC: [%p]\n", n);

    for ( DGNode *n : dependenceEdges )
        fprintf(stderr, "\tD: [%p]\n", n);

    for ( DGNode *n : revDependenceEdges )
        fprintf(stderr, "\trD: [%p]\n", n);
}

void DependenceGraph::dump(void) const
{
    for ( auto n : nodes )
        n->dump();
}

static void dump_to_dot(const DGNode *n, FILE *f)
{
    for (auto I = n->control_begin(), E = n->control_end();
         I != E; ++I)
        fprintf(f, "\tNODE%p -> NODE%p;\n", n, *I);
    for (auto I = n->dependence_begin(), E = n->dependence_end();
         I != E; ++I)
        fprintf(f, "\tNODE%p -> NODE%p [color=red];\n", n, *I);
}

bool DependenceGraph::dumpToDot(const char *file,
                                const char *description)
{
    // we have stdio included, do not use streams for such
    // easy task
    FILE *out = fopen(file, "w");
    if (!out) {
        fprintf(stderr, "Failed opening file %s\n", file);
        return false;
    }

    fprintf(out, "digraph \"%s\" {\n",
            description ? description : "DependencyGraph");

    // if we have entry node, use it as a root
    // otherwise just dump the graph somehow
    if (entryNode) {
        DFS(entryNode, dump_to_dot, out);
    } else {
        for (DGNode *n : nodes) {
            dump_to_dot(n, out);
        }
    }

    fprintf(out, "}\n");
    fclose(out);
}

// --------------------------------------------------------
// --- DGNode
// --------------------------------------------------------

DGNode::DGNode()
:subgraph(NULL), parameters(NULL)
{
    DBG(NODES, "Created node [%p]", this);
}

std::pair<DependenceGraph *, DependenceGraph *>
DGNode::getSubgraphWithParams(void) const
{
    return std::pair<DependenceGraph *,
                     DependenceGraph *>    (subgraph, parameters);
}

bool DGNode::addControlEdge(DGNode *n)
{
#ifdef DEBUG_ENABLED
    bool ret1, ret2;

    ret1 = n->revControlEdges.insert(this).second;
    ret2 = controlEdges.insert(n).second;

    // we either have both edges or none
    assert(ret1 == ret2);

    DBG(CONTROL, "Added control edge [%p]->[%p]\n", this, n);

    return ret2;
#else
    n->revControlEdges.insert(this);
    return controlEdges.insert(n).second;
#endif
}

bool DGNode::addDependenceEdge(DGNode *n)
{
#ifdef DEBUG_ENABLED
    bool ret1, ret2;

    ret1 = n->revDependenceEdges.insert(this).second;
    ret2 = dependenceEdges.insert(n).second;

    assert(ret1 == ret2);

    DBG(DEPENDENCE, "Added dependence edge [%p]->[%p]\n", this, n);

    return ret2;
#else
n->revDependenceEdges.insert(this);
    return dependenceEdges.insert(n).second;
#endif
}

DependenceGraph *DGNode::addSubgraph(DependenceGraph *sub)
{
    DependenceGraph *old = subgraph;
    subgraph = sub;

    return old;
}

DependenceGraph *DGNode::addParameters(DependenceGraph *params)
{
    DependenceGraph *old = parameters;

    assert(subgraph && "BUG: setting parameters without subgraph");

    parameters = params;
    return old;
}

// --------------------------------------------------------
// --- DependenceGraph
// --------------------------------------------------------

DependenceGraph::DependenceGraph()
:entryNode(NULL), nodes_num(0)
{
#ifdef DEBUG_ENABLED
    debug::init();
#endif
}

DGNode *DependenceGraph::setEntry(DGNode *n)
{
    DGNode *oldEnt = entryNode;
    entryNode = n;

    return oldEnt;
}

bool DependenceGraph::addNode(DGNode *n)
{
    bool ret = nodes.insert(n).second;
    if (ret)
        ++nodes_num;

    return ret;
}

DGNode *DependenceGraph::removeNode(DGNode *n)
{
    // XXX remove the edges
    --nodes_num;
    return n;
}


} // namespace dg
