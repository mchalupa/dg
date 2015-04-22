/// XXX add licence
//

#ifndef _DEPENDENCE_GRAPH_H_
#define _DEPENDENCE_GRAPH_H_

#include <utility>
#include <set>
#include <queue>
#include <map>
#include <cassert>

namespace dg {

template <typename DG, typename KeyT, typename NodePtrT>
class Node;

#ifdef ENABLE_CFG

/// ------------------------------------------------------------------
// - BBlock
//     Basic block structure for dependence graph
/// ------------------------------------------------------------------
template <typename NodePtrT>
class BBlock
{
public:
    BBlock<NodePtrT>(NodePtrT first, NodePtrT last = nullptr)
        : firstNode(first), lastNode(last), dfs_run(0)
#if defined(ENABLE_POSTDOM)
          , ipostdom(nullptr)
#endif
    {
        first->setBasicBlock(this);
        if (last)
            last->setBasicBlock(this);
    }

    // TODO use llvm::SmallPtrSet if we have llvm
    typedef std::set<BBlock<NodePtrT> *> ContainerT;

    const ContainerT& successors() const { return nextBBs; }
    const ContainerT& predcessors() const { return prevBBs; }

    typename ContainerT::size_type successorsNum() const
    {
        return nextBBs.size();
    }

    typename ContainerT::size_type predcessorsNum() const
    {
        return prevBBs.size();
    }

    bool addSuccessor(BBlock<NodePtrT> *b)
    {
        bool ret, ret2;
        ret = nextBBs.insert(b).second;
        ret2 = b->prevBBs.insert(this).second;

        // we either have both edges or none
        assert(ret == ret2);

        return ret;
    }

    void removeSuccessors()
    {
        for (auto BB : nextBBs) {
            BB->prevBBs.erase(this);
        }

        nextBBs.clear();
    }

    void removePredcessors()
    {
        for (auto BB : prevBBs) {
            BB->nextBBs.erase(this);
        }

        prevBBs.clear();
    }

    // remove predcessor basic block. Return value is
    // 0 if nothing was removed, 1 if only one edge was removed
    // (asserted when NDEBUG is defined)
    // and two if both edges were removed.
    // (Edges are [this -> p] and [p -> this])
    size_t removePredcessor(BBlock<NodePtrT> *p)
    {
        size_t ret = 0;
        ret += p->nextBBs.erase(this);
        ret += prevBBs.erase(p);

        // return value 1 means bug
        assert(ret != 1 && "Bug in edges between basic blocks");

        return ret;
    }

    // return value is the same as with removePredcessor
    size_t removeSuccessor(BBlock<NodePtrT> *p)
    {
        size_t ret = 0;
        ret += p->prevBBs.erase(this);
        ret += nextBBs.erase(p);

        // return value 1 means bug
        assert(ret != 1 && "Bug in edges between basic blocks");

        return ret;
    }

    NodePtrT getFirstNode() const { return firstNode; }
    NodePtrT getLastNode() const { return lastNode; }
    NodePtrT setLastNode(NodePtrT nn)
    {
        NodePtrT old = lastNode;
        lastNode = nn;
        return old;
    }

#if defined(ENABLE_POSTDOM)
    // get immediate post-dominator
    const ContainerT& getIPostDom() const { return ipostdominates; }
    BBlock<NodePtrT> *getIPostDomBy() const { return ipostdom; }
    // add node that is immediately post-dominated by this node
    bool addIPostDom(BBlock<NodePtrT> *pd)
    {
        assert(pd && "Passed nullptr");

        if (pd == ipostdom)
            return false;

        /*
        assert((pd == nullptr || pd == ipostdom)
               && "Node already has different post-dominator");
        */

        ipostdom = pd;
        return pd->ipostdominates.insert(this).second;
    }
#endif // ENABLE_POSTDOM

    unsigned int getDFSRun() const { return dfs_run; }
    void setDFSRun(unsigned int id)
    {
        dfs_run = id;
    }

private:
    ContainerT nextBBs;
    ContainerT prevBBs;

    // first node in this basic block
    NodePtrT firstNode;
    // last node in this basic block
    NodePtrT lastNode;

#if defined(ENABLE_POSTDOM)
    // immediate postdominator. The BB can be immediate
    // post-dominator of more nodes
    ContainerT ipostdominates;
    // reverse edge to immediate postdom. The BB can be
    // immediately post-dominated only by one BB
    BBlock<NodePtrT> *ipostdom;
#endif // ENABLE_POSTDOM

    // helper variable for running DFS/BFS on the BasicBlocks
    unsigned int dfs_run;
};

#endif // ENABLE_CFG

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
    // TODO when LLVM is enabled, use SmallPtrSet
    // else BDD ?
    typedef std::set<NodePtrT> ControlEdgesType;
    typedef std::set<NodePtrT> DependenceEdgesType;

    typedef typename ControlEdgesType::iterator control_iterator;
    typedef typename ControlEdgesType::const_iterator const_control_iterator;
    typedef typename DependenceEdgesType::iterator dependence_iterator;
    typedef typename DependenceEdgesType::const_iterator const_dependence_iterator;

    Node<DG, KeyT, NodePtrT>(const KeyT& k)
        : key(k), parameters(nullptr), dfs_run(0)
#if ENABLE_CFG
         , basicBlock(nullptr), nextNode(nullptr), prevNode(nullptr)
#endif
    {
    }

    bool addControlDependence(NodePtrT n)
    {
        bool ret1, ret2;

        ret1 = n->revControlDepEdges.insert(static_cast<NodePtrT>(this)).second;
        ret2 = controlDepEdges.insert(n).second;

        // we either have both edges or none
        assert(ret1 == ret2);

        return ret2;
    }

    bool addDataDependence(NodePtrT n)
    {
        bool ret1, ret2;

        ret1 = n->revDataDepEdges.insert(static_cast<NodePtrT>(this)).second;
        ret2 = dataDepEdges.insert(n).second;

        assert(ret1 == ret2);

        return ret2;
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

    ControlEdgesType controlDepEdges;
    DependenceEdgesType dataDepEdges;

    // Nodes that have control/dep edge to this node
    ControlEdgesType revControlDepEdges;
    DependenceEdgesType revDataDepEdges;

    // a node can have more subgraphs (i. e. function pointers)
    std::set<DG *> subgraphs;

    // instead of adding parameter in/out nodes to parent
    // graph, we create new small graph just with these
    // nodes and summary edges (as dependence edges)
    // parameters are shared for all subgraphs
    DG *parameters;

    // last id of DFS that ran on this node
    // ~~> marker if it has been processed
    unsigned int dfs_run;
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
    :entryNode(nullptr), exitNode(nullptr), dfs_run(0), refcount(1)
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
        unsigned int run_id = ++dfs_run;
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
        unsigned int run_id = ++dfs_run;
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
    void DFSProcessEdges(IT begin, IT end, Q& queue, unsigned int run_id)
    {
        for (IT I = begin; I != end; ++I) {
            ValueType tmp = *I;
            if (tmp->getDFSrun() == run_id)
                continue;

            // mark node as visited
            tmp->setDFSrun(run_id);
            queue.push(tmp);
        }
    }

    ValueType entryNode;
    ValueType exitNode;

#ifdef ENABLE_CFG
    BBlock<ValueType> *entryBB;
    BBlock<ValueType> *exitBB;
#endif // ENABLE_CFG

    // counter for dfs_runs
    unsigned int dfs_run;

    // how many nodes keeps pointer to this graph?
    int refcount;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
