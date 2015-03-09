/// XXX add licence
//

#ifndef _DEPENDENCE_GRAPH_H_
#define _DEPENDENCE_GRAPH_H_

#include <utility>
#include <set>
#include <queue>
#include <cassert>

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
    unsigned int getDFSrun(void) const { return dfs_run; }
    unsigned int setDFSrun(unsigned int r) { dfs_run = r; }

    void dump(void) const;

    /* control dependency edges iterators */
    control_iterator control_begin(void) { return controlEdges.begin(); }
    const_control_iterator control_begin(void) const { return controlEdges.begin(); }
    control_iterator control_end(void) { return controlEdges.end(); }
    const_control_iterator control_end(void) const { return controlEdges.end(); }

    /* reverse control dependency edges iterators */
    control_iterator rev_control_begin(void) { return revControlEdges.begin(); }
    const_control_iterator rev_control_begin(void) const { return revControlEdges.begin(); }
    control_iterator rev_control_end(void) { return revControlEdges.end(); }
    const_control_iterator rev_control_end(void) const { return revControlEdges.end(); }

    /* data dependency edges iterators */
    dependence_iterator dependence_begin(void) { return dependenceEdges.begin(); }
    const_dependence_iterator dependence_begin(void) const { return dependenceEdges.begin(); }
    dependence_iterator dependence_end(void) { return dependenceEdges.end(); }
    const_dependence_iterator dependence_end(void) const { return dependenceEdges.end(); }

    /* reverse data dependency edges iterators */
    dependence_iterator rev_dependence_begin(void) { return revDependenceEdges.begin(); }
    const_dependence_iterator rev_dependence_begin(void) const { return revDependenceEdges.begin(); }
    dependence_iterator rev_dependence_end(void) { return revDependenceEdges.end(); }
    const_dependence_iterator rev_dependence_end(void) const { return revDependenceEdges.end(); }

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

    // last id of DFS that ran on this node
    // ~~> marker if it has been processed
    unsigned int dfs_run;
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
    bool dumpToDot(const char *file, const char *description = NULL);
    const unsigned int getNodesNum(void) const { return nodes_num; }

    iterator begin(void) { return nodes.begin(); }
    const_iterator begin(void) const { return nodes.begin(); }
    iterator end(void) { return nodes.end(); }
    const_iterator end(void) const { return nodes.end(); }

    // make DFS on graph, using control and/or deps edges
    template <typename F, typename D>
    void DFS(DGNode *entry, F func, D data,
             bool control = true, bool deps = true)
    {
        unsigned int run_id = ++dfs_run;
        std::queue<DGNode *> queue;

        assert(entry && "Need entry node for DFS");

        queue.push(entry);

        while (!queue.empty()) {
            DGNode *n = queue.front();
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
    void revDFS(DGNode *entry, F func, D data,
                bool control = true, bool deps = true)
    {
        unsigned int run_id = ++dfs_run;
        std::queue<DGNode *> queue;

        assert(entry && "Need entry node for DFS");

        queue.push(entry);

        while (!queue.empty()) {
            DGNode *n = queue.front();
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
            DGNode *tmp = *I;
            if (tmp->getDFSrun() == run_id)
                continue;

            // mark node as visited
            tmp->setDFSrun(run_id);
            queue.push(tmp);
        }
    }

    DGNode *entryNode;
    ContainerType nodes;
    unsigned int nodes_num;
    // counter for dfs_runs
    unsigned int dfs_run;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
