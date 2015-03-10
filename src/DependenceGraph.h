/// XXX add licence
//

#ifndef _DEPENDENCE_GRAPH_H_
#define _DEPENDENCE_GRAPH_H_

#include <utility>
#include <set>
#include <queue>
#include <cassert>
#include <cstdio>

#include "DGUtil.h"

namespace dg {

template <typename Key>
class DependenceGraph;

/// --------------------------------------------------------
//  -- DGNode
//     one node in DependenceGraph
/// --------------------------------------------------------
template <typename Key>
class DGNode
{
public:
    // TODO when LLVM is enabled, use SmallPtrSet
    // else BDD ?
    typedef std::set<DGNode<Key> *> ControlEdgesType;
    typedef std::set<DGNode<Key> *> DependenceEdgesType;

    typedef typename ControlEdgesType::iterator control_iterator;
    typedef typename ControlEdgesType::const_iterator const_control_iterator;
    typedef typename DependenceEdgesType::iterator dependence_iterator;
    typedef typename DependenceEdgesType::const_iterator const_dependence_iterator;

    DGNode<Key>(Key k)
    :subgraph(NULL), parameters(NULL), dfs_run(0), key(k)
    {
        DBG(NODES, "Created node [%p]", this);
    }

    bool addControlEdge(DGNode<Key> *n)
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

    bool addDependenceEdge(DGNode<Key> *n)
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


    unsigned int getDFSrun(void) const { return dfs_run; }
    unsigned int setDFSrun(unsigned int r) { dfs_run = r; }

    // control dependency edges iterators
    control_iterator control_begin(void) { return controlEdges.begin(); }
    const_control_iterator control_begin(void) const { return controlEdges.begin(); }
    control_iterator control_end(void) { return controlEdges.end(); }
    const_control_iterator control_end(void) const { return controlEdges.end(); }

    // reverse control dependency edges iterators
    control_iterator rev_control_begin(void) { return revControlEdges.begin(); }
    const_control_iterator rev_control_begin(void) const { return revControlEdges.begin(); }
    control_iterator rev_control_end(void) { return revControlEdges.end(); }
    const_control_iterator rev_control_end(void) const { return revControlEdges.end(); }

    // data dependency edges iterators
    dependence_iterator dependence_begin(void) { return dependenceEdges.begin(); }
    const_dependence_iterator dependence_begin(void) const { return dependenceEdges.begin(); }
    dependence_iterator dependence_end(void) { return dependenceEdges.end(); }
    const_dependence_iterator dependence_end(void) const { return dependenceEdges.end(); }

    // reverse data dependency edges iterators
    dependence_iterator rev_dependence_begin(void) { return revDependenceEdges.begin(); }
    const_dependence_iterator rev_dependence_begin(void) const { return revDependenceEdges.begin(); }
    dependence_iterator rev_dependence_end(void) { return revDependenceEdges.end(); }
    const_dependence_iterator rev_dependence_end(void) const { return revDependenceEdges.end(); }

    DependenceGraph<Key> *addSubgraph(DependenceGraph<Key> *sub)
    {
        DependenceGraph<Key> *old = subgraph;
        subgraph = sub;

        return old;
    }

    DependenceGraph<Key> *addParameters(DependenceGraph<Key> *params)
    {
        DependenceGraph<Key> *old = parameters;

        assert(subgraph && "BUG: setting parameters without subgraph");

        parameters = params;
        return old;
    }

    DependenceGraph<Key> *getSubgraph(void) const { return subgraph; }
    DependenceGraph<Key> *getParameters(void) const { return parameters; }

    std::pair<DependenceGraph<Key> *,
              DependenceGraph<Key> *> getSubgraphWithParams(void) const
    {
        return std::pair<DependenceGraph<Key> *,
                         DependenceGraph<Key> *>(subgraph, parameters);
    }

    Key getKey(void) { return key; }

private:
    // this is specific value that identifies this node
    Key key;

    ControlEdgesType controlEdges;
    DependenceEdgesType dependenceEdges;

    // Nodes that have control/dep edge to this node
    ControlEdgesType revControlEdges;
    DependenceEdgesType revDependenceEdges;

    DependenceGraph<Key> *subgraph;
    // instead of adding parameter in/out nodes to parent
    // graph, we create new small graph just with these
    // nodes and summary edges (as dependence edges)
    DependenceGraph<Key> *parameters;

    // last id of DFS that ran on this node
    // ~~> marker if it has been processed
    unsigned int dfs_run;
};

// --------------------------------------------------------
// --- DependenceGraph
// --------------------------------------------------------
template <typename Key>
class DependenceGraph
{
public:
    DependenceGraph<Key>()
    :entryNode(NULL), nodes_num(0), dfs_run(0)
    {
#ifdef DEBUG_ENABLED
        debug::init();
#endif
    }

    virtual ~DependenceGraph<Key>() {}

    DGNode<Key> *setEntry(DGNode<Key> *n)
    {
        DGNode<Key> *oldEnt = entryNode;
        entryNode = n;

        return oldEnt;
    }

    DGNode<Key> *getEntry(void) const { return entryNode; }

    bool addNode(DGNode<Key> *n)
    {
        Key k = n->getKey();
    }

    DGNode<Key> *removeNode(Key k);

    const unsigned int getNodesNum(void) const { return nodes_num; }

    // make DFS on graph, using control and/or deps edges
    template <typename F, typename D>
    void DFS(DGNode<Key> *entry, F func, D data,
             bool control = true, bool deps = true)
    {
        unsigned int run_id = ++dfs_run;
        std::queue<DGNode<Key> *> queue;

        assert(entry && "Need entry node for DFS");

        queue.push(entry);

        while (!queue.empty()) {
            DGNode<Key> *n = queue.front();
            queue.pop();

            func(n, data);

            // add unprocessed vertices
            if (control)
                DFSProcessEdges(n->control_begin(),
                                n->control_end(), queue, run_id);

            if (deps)
                DFSProcessEdges(n->dependence_begin(),
                                n->dependence_end(), queue, run_id);
        }
    }

    template <typename F, typename D>
    void revDFS(DGNode<Key> *entry, F func, D data,
                bool control = true, bool deps = true)
    {
        unsigned int run_id = ++dfs_run;
        std::queue<DGNode<Key> *> queue;

        assert(entry && "Need entry node for DFS");

        queue.push(entry);

        while (!queue.empty()) {
            DGNode<Key> *n = queue.front();
            queue.pop();

            func(n, data);

            // add unprocessed vertices
            if (control)
                DFSProcessEdges(n->rev_control_begin(),
                                n->rev_control_end(), queue, run_id);

            if (deps)
                DFSProcessEdges(n->rev_dependence_begin(),
                                n->rev_dependence_end(), queue, run_id);
        }
    }

private:
    template <typename Q, typename IT>
    void DFSProcessEdges(IT begin, IT end, Q& queue, unsigned int run_id)
    {
        for (IT I = begin; I != end; ++I) {
            DGNode<Key> *tmp = *I;
            if (tmp->getDFSrun() == run_id)
                continue;

            // mark node as visited
            tmp->setDFSrun(run_id);
            queue.push(tmp);
        }
    }

    DGNode<Key> *entryNode;
    unsigned int nodes_num;
    // counter for dfs_runs
    unsigned int dfs_run;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
