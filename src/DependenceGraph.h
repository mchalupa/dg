/// XXX add licence
//

#ifndef _DEPENDENCE_GRAPH_H_
#define _DEPENDENCE_GRAPH_H_

#include <utility>
#include <set>
#include <queue>
#include <map>
#include <cassert>

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

#ifdef ENABLE_CFG
    typedef std::set<DGNode<Key> *> CFGEdgesType;
    typedef typename CFGEdgesType::iterator cfg_iterator;
    typedef typename CFGEdgesType::const_iterator const_cfg_iterator;
#endif /* ENABLE_CFG */

    DGNode<Key>(Key k)
    :subgraph(NULL), parameters(NULL), dfs_run(0), key(k)
    {
        DBG(NODES, "Created node [%p]", this);
    }

    bool addControlDependence(DGNode<Key> *n)
    {
#ifdef DEBUG_ENABLED
        bool ret1, ret2;

        ret1 = n->revControlDepEdges.insert(this).second;
        ret2 = controlDepEdges.insert(n).second;

        // we either have both edges or none
        assert(ret1 == ret2);

        DBG(CONTROL, "Added control edge [%p]->[%p]\n", this, n);

        return ret2;
#else
        n->revControlDepEdges.insert(this);
        return controlDepEdges.insert(n).second;
#endif
    }

    bool addDataDependence(DGNode<Key> *n)
    {
#ifdef DEBUG_ENABLED
        bool ret1, ret2;

        ret1 = n->revDataDepEdges.insert(this).second;
        ret2 = dataDepEdges.insert(n).second;

        assert(ret1 == ret2);

        DBG(DEPENDENCE, "Added dependence edge [%p]->[%p]\n", this, n);

        return ret2;
#else
        n->revDataDepEdges.insert(this);
        return dataDepEdges.insert(n).second;
#endif
    }


    unsigned int getDFSrun(void) const { return dfs_run; }
    unsigned int setDFSrun(unsigned int r) { dfs_run = r; }

    // control dependency edges iterators
    control_iterator control_begin(void) { return controlDepEdges.begin(); }
    const_control_iterator control_begin(void) const { return controlDepEdges.begin(); }
    control_iterator control_end(void) { return controlDepEdges.end(); }
    const_control_iterator control_end(void) const { return controlDepEdges.end(); }

    // reverse control dependency edges iterators
    control_iterator rev_control_begin(void) { return revControlDepEdges.begin(); }
    const_control_iterator rev_control_begin(void) const { return revControlDepEdges.begin(); }
    control_iterator rev_control_end(void) { return revControlDepEdges.end(); }
    const_control_iterator rev_control_end(void) const { return revControlDepEdges.end(); }

    // data dependency edges iterators
    dependence_iterator dependence_begin(void) { return dataDepEdges.begin(); }
    const_dependence_iterator dependence_begin(void) const { return dataDepEdges.begin(); }
    dependence_iterator dependence_end(void) { return dataDepEdges.end(); }
    const_dependence_iterator dependence_end(void) const { return dataDepEdges.end(); }

    // reverse data dependency edges iterators
    dependence_iterator rev_dependence_begin(void) { return revDataDepEdges.begin(); }
    const_dependence_iterator rev_dependence_begin(void) const { return revDataDepEdges.begin(); }
    dependence_iterator rev_dependence_end(void) { return revDataDepEdges.end(); }
    const_dependence_iterator rev_dependence_end(void) const { return revDataDepEdges.end(); }

#ifdef ENABLE_CFG
    void addSucc(DGNode<Key> *s) { succs.insert(s); }

    cfg_iterator succ_begin(void) { return succs.begin(); }
    const_cfg_iterator succ_begin(void) const { return succs.begin(); }
    cfg_iterator succ_end(void) { return succs.end(); }
    const_cfg_iterator succ_end(void) const { return succs.end(); }
    unsigned int getSuccNum(void) const { return succs.size(); }
#endif /* ENABLE_CFG */

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
    const Key getKey(void) const { return key; }

private:
    // this is specific value that identifies this node
    Key key;

#ifdef ENABLE_CFG
    // some analyses need classical CFG edges

    // successors of this node
    CFGEdgesType succs;
#endif /* ENABLE_CFG */

    ControlEdgesType controlDepEdges;
    DependenceEdgesType dataDepEdges;

    // Nodes that have control/dep edge to this node
    ControlEdgesType revControlDepEdges;
    DependenceEdgesType revDataDepEdges;

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
    typedef DGNode<Key>* ValueType;
    typedef std::map<Key, ValueType> ContainerType;
    typedef typename ContainerType::iterator iterator;
    typedef typename ContainerType::const_iterator const_iterator;

    DependenceGraph<Key>()
    :entryNode(NULL), dfs_run(0)
    {
#ifdef DEBUG_ENABLED
        debug::init();
#endif
    }

    virtual ~DependenceGraph<Key>() {}

    // iterators
    iterator begin(void) { return nodes.begin(); }
    const_iterator begin(void) const { return nodes.begin(); }
    iterator end(void) { return nodes.end(); }
    const_iterator end(void) const { return nodes.end(); }

    ValueType operator[](Key k) { return nodes[k]; }
    const ValueType operator[](Key k) const { return nodes[k]; }
    // reference getter for fast include-if-null operation
    ValueType& getRef(Key k) { return nodes[k]; }
    bool contains(Key k) const { return nodes.count(k) != 0; }

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
	nodes.insert(std::make_pair(k, n));
    }

    DGNode<Key> *removeNode(Key k)
    {
	auto n = nodes.find(n);
	if (n == nodes.end())
		return NULL;

	nodes.erase(n);

	// remove edges
	assert(0 && "Remove edges");

	return n;
    }

    DGNode<Key> *removeNode(DGNode<Key> *n)
    {
	return removeNode(n->getKey());
    }

    const unsigned int getSize(void) const { return nodes.size(); }

    // make DFS on graph, using control and/or deps edges
    template <typename F, typename D>
    void DFS(DGNode<Key> *entry, F func, D data,
             bool control = true, bool deps = true)
    {
        unsigned int run_id = ++dfs_run;
        std::queue<DGNode<Key> *> queue;

        assert(entry && "Need entry node for DFS");

        entry->setDFSrun(run_id);
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

        entry->setDFSrun(run_id);
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
    ContainerType nodes;

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

    // counter for dfs_runs
    unsigned int dfs_run;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
