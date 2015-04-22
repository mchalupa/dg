/// XXX add licence
//

#ifndef _DEPENDENCE_GRAPH_H_
#define _DEPENDENCE_GRAPH_H_

#include <utility>
#include <queue>
#include <map>
#include <cassert>

#include "BBlock.h"
#include "EdgesContainer.h"

namespace dg {

template <typename DG, typename KeyT, typename NodePtrT>
class Node;

template <typename Key, typename ValueType>
class DependenceGraph;

/// --------------------------------------------------------
//  -- Node
//     one node in DependenceGraph
/// --------------------------------------------------------
template <typename DG, typename KeyT, typename NodePtrT>
class Node
{
public:
    typedef EdgesContainer<NodePtrT> ControlEdgesT;
    typedef EdgesContainer<NodePtrT> DependenceEdgesT;

    typedef typename ControlEdgesT::iterator control_iterator;
    typedef typename ControlEdgesT::const_iterator const_control_iterator;
    typedef typename DependenceEdgesT::iterator dependence_iterator;
    typedef typename DependenceEdgesT::const_iterator const_dependence_iterator;

    Node<DG, KeyT, NodePtrT>(const KeyT& k)
        : key(k), parameters(nullptr)
#if ENABLE_CFG
         , basicBlock(nullptr), nextNode(nullptr),
           prevNode(nullptr)
#endif
    {
    }

    bool addControlDependence(NodePtrT n)
    {
        bool ret1, ret2;

        ret1 = n->revControlDepEdges.insert(static_cast<NodePtrT>(this));
        ret2 = controlDepEdges.insert(n);

        // we either have both edges or none
        assert(ret1 == ret2);

        return ret2;
    }

    bool addDataDependence(NodePtrT n)
    {
        bool ret1, ret2;

        ret1 = n->revDataDepEdges.insert(static_cast<NodePtrT>(this));
        ret2 = dataDepEdges.insert(n);

        assert(ret1 == ret2);

        return ret2;
    }

    unsigned int getDFSRunId(void) const
    {
        return Analyses.dfsrunid;
    }

    // increase and return DFS runid
    unsigned int incDFSRunId(void) const
    {
        return ++Analyses.dfsrunid;
    }

    void setDFSRunId(unsigned int r)
    {
        Analyses.dfsrunid = r;
    }

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

    unsigned int getControlDependenciesNum() const { return controlDepEdges.size(); }
    unsigned int getRevControlDependenciesNum() const { return revControlDepEdges.size(); }
    unsigned int getDataDependenciesNum() const { return dataDepEdges.size(); }
    unsigned int getRevDataDependenciesNum() const { return revDataDepEdges.size(); }

#ifdef ENABLE_CFG
    BBlock<NodePtrT> *getBasicBlock() { return basicBlock; }
    const BBlock<NodePtrT> *getBasicBlock() const { return basicBlock; }

    BBlock<NodePtrT> *setBasicBlock(BBlock<NodePtrT> *nbb)
    {
        BBlock<NodePtrT> *old = basicBlock;
        basicBlock = nbb;
        return old;
    }

    NodePtrT addSuccessor(NodePtrT s)
    {
        NodePtrT old = nextNode;
        nextNode = s;

        s->prevNode = static_cast<NodePtrT>(this);

        return old;
    }

    bool hasSuccessor() const { return nextNode != nullptr; }
    bool hasPredcessor() const { return prevNode != nullptr; }

    const NodePtrT getSuccessor() const { return nextNode; }
    const NodePtrT getPredcessor() const { return prevNode; }
    NodePtrT getSuccessor() { return nextNode; }
    NodePtrT getPredcessor() { return prevNode; }

#endif /* ENABLE_CFG */

    bool addSubgraph(DG *sub)
    {
        bool ret = subgraphs.insert(sub).second;

        if (ret) {
            // increase references of this graph
            // if we added it
            sub->ref();
        }

        return ret;
    }

    DG *addParameters(DG *params)
    {
        DG *old = parameters;

        assert(hasSubgraphs() && "BUG: setting parameters without subgraph");

        parameters = params;
        return old;
    }

    const std::set<DG *>& getSubgraphs(void) const
    {
        return subgraphs;
    }

    bool hasSubgraphs() const
    {
        return !subgraphs.empty();
    }

    size_t subgraphsNum() const
    {
        return subgraphs.size();
    }

    DG *getParameters() const
    {
        return parameters;
    }

    KeyT getKey() const
    {
        return key;
    }

protected:

    // key uniquely identifying this node in a graph
    KeyT key;

private:

#ifdef ENABLE_CFG
    // some analyses need classical CFG edges
    // and it is better to have even basic blocks
    BBlock<NodePtrT> *basicBlock;

    // successors of this node
    NodePtrT nextNode;
    // predcessors of this node
    NodePtrT prevNode;
#endif /* ENABLE_CFG */

    ControlEdgesT controlDepEdges;
    DependenceEdgesT dataDepEdges;

    // Nodes that have control/dep edge to this node
    ControlEdgesT revControlDepEdges;
    DependenceEdgesT revDataDepEdges;

    // a node can have more subgraphs (i. e. function pointers)
    std::set<DG *> subgraphs;

    // instead of adding parameter in/out nodes to parent
    // graph, we create new small graph just with these
    // nodes and summary edges (as dependence edges)
    // parameters are shared for all subgraphs
    DG *parameters;

    // auxiliary varibales for different analyses
    struct _Analysis
    {
        _Analysis() : dfsrunid(0) {}

        // last id of DFS that ran on this node
        // ~~> marker if it has been processed
        unsigned int dfsrunid;
    } Analyses;

};

// --------------------------------------------------------
// --- DependenceGraph
// --------------------------------------------------------
template <typename Key, typename ValueType>
class DependenceGraph
{
public:
    typedef std::map<Key, ValueType> ContainerType;
    typedef typename ContainerType::iterator iterator;
    typedef typename ContainerType::const_iterator const_iterator;

    DependenceGraph<Key, ValueType>()
    :entryNode(nullptr), exitNode(nullptr),
     dfs_run_counter(0), refcount(1)
#ifdef ENABLE_CFG
     , entryBB(nullptr), exitBB(nullptr)
#endif
    {
    }

    // TODO add copy constructor for cloning graph

    virtual ~DependenceGraph<Key, ValueType>() {}

    // iterators
    iterator begin(void) { return nodes.begin(); }
    const_iterator begin(void) const { return nodes.begin(); }
    iterator end(void) { return nodes.end(); }
    const_iterator end(void) const { return nodes.end(); }

    ValueType operator[](Key k) { return nodes[k]; }
    const ValueType operator[](Key k) const { return nodes[k]; }
    // reference getter for fast include-if-null operation
    ValueType& getRef(Key k) { return nodes[k]; }
    ValueType getNode(Key k) { return nodes[k]; }
    bool contains(Key k) const { return nodes.count(k) != 0; }
    iterator find(Key k) { return nodes.find(k); }
    const_iterator find(Key k) const { return nodes.find(k); }

    ValueType setEntry(ValueType n)
    {
        ValueType oldEnt = entryNode;
        entryNode = n;

        return oldEnt;
    }

    ValueType setExit(ValueType n)
    {
        ValueType oldExt = exitNode;
        exitNode = n;

        return oldExt;
    }

    ValueType getEntry(void) const { return entryNode; }
    ValueType getExit(void) const { return exitNode; }

    // dependence graph can be shared between more call-sites that
    // has references to this graph. When destroying graph, we
    // must be sure do delete it just once, so count references
    // XXX this is up to user if she uses ref()/unref() methods
    // or handle these stuff some other way
    int ref()
    {
        ++refcount;
        return refcount;
    }

    // unref graph and delete if refrences drop to 0
    // destructor calls this on subgraphs
    int unref(bool deleteOnZero = true)
    {
        --refcount;

        if (deleteOnZero && refcount == 0) {
            delete this;
            return 0;
        }

        return refcount;
    }

#ifdef ENABLE_CFG
    BBlock<ValueType> *getEntryBB() const { return entryBB; }
    BBlock<ValueType> *getExitBB() const { return exitBB; }

    BBlock<ValueType> *setEntryBB(BBlock<ValueType> *nbb)
    {
        BBlock<ValueType> *old = entryBB;
        entryBB = nbb;

        return old;
    }

    BBlock<ValueType> *setExitBB(BBlock<ValueType> *nbb)
    {
        BBlock<ValueType> *old = exitBB;
        exitBB = nbb;

        return old;
    }
#endif // ENABLE_CFG

    // add a node to this graph. The DependenceGraph is something like
    // namespace for nodes, since every node has unique key and we can
    // have another node with same key in another
    // graph. So we can have two nodes for the same value but in different
    // graphs. The edges can be between arbitrary nodes and do not
    // depend on graphs the nodes are in.
    /* virtual */ bool addNode(Key k, ValueType n)
    {
        nodes.insert(std::make_pair(k, n));
    }

    // make it virtual? We don't need it now, but
    // in the future it may be handy.
    /* virtual */ bool addNode(ValueType n)
    {
        // ValueType is a class derived from
        // dg::Node, so it must have getKey() method
        return addNode(n->getKey(), n);
    }

    ValueType removeNode(Key k)
    {
    auto n = nodes.find(n);
    if (n == nodes.end())
        return nullptr;

    nodes.erase(n);

    // remove edges
    assert(0 && "Remove edges");

    return n;
    }

    const unsigned int getSize(void) const { return nodes.size(); }

    // make DFS on graph, using control and/or deps edges
    template <typename F, typename D>
    void DFS(ValueType entry, F func, D data,
             bool control = true, bool deps = true)
    {
        unsigned int run_id = ++dfs_run_counter;
        std::queue<ValueType> queue;

        assert(entry && "Need entry node for DFS");

        entry->setDFSRunId(run_id);
        queue.push(entry);

        while (!queue.empty()) {
            ValueType n = queue.front();
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
    void revDFS(ValueType entry, F func, D data,
                bool control = true, bool deps = true)
    {
        unsigned int run_id = ++dfs_run_counter;
        std::queue<ValueType> queue;

        assert(entry && "Need entry node for DFS");

        entry->setDFSrun(run_id);
        queue.push(entry);

        while (!queue.empty()) {
            ValueType n = queue.front();
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

protected:
    // nodes contained in this dg. They are protected, so that
    // child classes can access them directly
    ContainerType nodes;

private:

    template <typename Q, typename IT>
    void DFSProcessEdges(IT begin, IT end, Q& queue,
                         unsigned int run_id)
    {
        for (IT I = begin; I != end; ++I) {
            ValueType tmp = *I;
            if (tmp->getDFSRunId() == run_id)
                continue;

            // mark node as visited
            tmp->setDFSRunId(run_id);
            queue.push(tmp);
        }
    }

    ValueType entryNode;
    ValueType exitNode;

#ifdef ENABLE_CFG
    BBlock<ValueType> *entryBB;
    BBlock<ValueType> *exitBB;
#endif // ENABLE_CFG

    // global counter for dfs runs
    unsigned int dfs_run_counter;

    // how many nodes keeps pointer to this graph?
    int refcount;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
