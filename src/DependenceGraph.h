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

template <typename DG, typename NodePtrT>
class Node;

#ifdef ENABLE_CFG

/// ------------------------------------------------------------------
// - DGBasicBlock
//     Basic block structure for dependence graph
/// ------------------------------------------------------------------
template <typename NodePtrT>
class DGBasicBlock
{
public:
    DGBasicBlock<NodePtrT>(NodePtrT first, NodePtrT last = nullptr)
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
    typedef std::set<DGBasicBlock<NodePtrT> *> ContainerT;

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

    bool addSuccessor(DGBasicBlock<NodePtrT> *b)
    {
        bool ret, ret2;
        ret = nextBBs.insert(b).second;
        ret2 = b->prevBBs.insert(this).second;

        // we either have both edges or none
        assert(ret == ret2);

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
    DGBasicBlock<NodePtrT> *getIPostDomBy() const { return ipostdom; }
    // add node that is immediately post-dominated by this node
    bool addIPostDom(DGBasicBlock<NodePtrT> *pd)
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
    DGBasicBlock<NodePtrT> *ipostdom;
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
template <typename DG, typename NodePtrT>
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

    Node<DG, NodePtrT>()
        :subgraph(nullptr), parameters(nullptr), dfs_run(0)
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
    DGBasicBlock<NodePtrT> *getBasicBlock() { return basicBlock; }
    const DGBasicBlock<NodePtrT> *getBasicBlock() const { return basicBlock; }

    DGBasicBlock<NodePtrT> *setBasicBlock(DGBasicBlock<NodePtrT> *nbb)
    {
        DGBasicBlock<NodePtrT> *old = basicBlock;
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

    DG *addSubgraph(DG *sub)
    {
        DG *old = subgraph;
        subgraph = sub;

        return old;
    }

    DG *addParameters(DG *params)
    {
        DG *old = parameters;

        assert(subgraph && "BUG: setting parameters without subgraph");

        parameters = params;
        return old;
    }

    DG *getSubgraph(void) const { return subgraph; }
    DG *getParameters(void) const { return parameters; }

    std::pair<DG *, DG *> getSubgraphWithParams(void) const
    {
        return std::make_pair(subgraph, parameters);
    }

private:

#ifdef ENABLE_CFG
    // some analyses need classical CFG edges
    // and it is better to have even basic blocks
    DGBasicBlock<NodePtrT> *basicBlock;

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

    DG *subgraph;
    // instead of adding parameter in/out nodes to parent
    // graph, we create new small graph just with these
    // nodes and summary edges (as dependence edges)
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
    :entryNode(nullptr), exitNode(nullptr), dfs_run(0)
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

#ifdef ENABLE_CFG
    DGBasicBlock<ValueType> *getEntryBB() const { return entryBB; }
    DGBasicBlock<ValueType> *getExitBB() const { return exitBB; }

    DGBasicBlock<ValueType> *setEntryBB(DGBasicBlock<ValueType> *nbb)
    {
        DGBasicBlock<ValueType> *old = entryBB;
        entryBB = nbb;

        return old;
    }

    DGBasicBlock<ValueType> *setExitBB(DGBasicBlock<ValueType> *nbb)
    {
        DGBasicBlock<ValueType> *old = exitBB;
        exitBB = nbb;

        return old;
    }
#endif // ENABLE_CFG

    bool addNode(Key k, ValueType n)
    {
        nodes.insert(std::make_pair(k, n));
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
    DGBasicBlock<ValueType> *entryBB;
    DGBasicBlock<ValueType> *exitBB;
#endif // ENABLE_CFG

    // counter for dfs_runs
    unsigned int dfs_run;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
