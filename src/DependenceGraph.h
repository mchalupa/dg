#ifndef _DEPENDENCE_GRAPH_H_
#define _DEPENDENCE_GRAPH_H_

#include <utility>
#include <queue>
#include <map>
#include <cassert>
#include <memory>

#include "BBlock.h"
#include "ADT/DGContainer.h"
#include "Node.h"

#include "analysis/Analysis.h"

namespace dg {

// -------------------------------------------------------------------
//  -- DependenceGraph
//
//  This is a base template for a dependence graphs. Every concrete
//  dependence graph will inherit from instance of this template.
//  Dependece graph has a map of nodes that it contains (each node
//  is required to have a unique key). Actually, there are two maps.
//  One for nodes that are local to the graph and one for nodes that
//  are global and can be shared between graphs.
//  Concrete dependence graph may not use all attributes of this class
//  and it is free to use them as it needs (e.g. it may use only
//  global nodes and thus share them between all graphs)
// -------------------------------------------------------------------
template <typename NodeT>
class DependenceGraph
{
public:
    // type of key that is used in nodes
    using KeyT = typename NodeT::KeyType;
    // type of this dependence graph - so that we can refer to it in the code
    using DependenceGraphT = typename NodeT::DependenceGraphType;

    using ContainerType = std::map<KeyT, NodeT *>;
    using iterator = typename ContainerType::iterator;
    using const_iterator = typename ContainerType::const_iterator;
#ifdef ENABLE_CFG
    using BBlocksMapT = std::map<KeyT, BBlock<NodeT> *>;
#endif

private:
    // entry and exit nodes of the graph
    NodeT *entryNode;
    NodeT *exitNode;

    // Formal parameters of the graph. Every graph is a graph of some function
    // and formal parameters are parameters from its prototype, i. e. for
    // foo(int a, int b) we have formal parameters 'a' and 'b'. Actual parameters
    // are the values that are passed to function call, so for foo(3, x)
    // the actual parameters are '3' and 'x'.
    // Actual parameters are stored in the call node.
    // Graph can have none or one formal parameters.
    DGParameters<NodeT> *formalParameters;

    // call-sites (nodes) that are calling this graph
    DGContainer<NodeT *> callers;

    // how many nodes keeps pointer to this graph?
    int refcount;

    // is the graph in some slice?
    uint64_t slice_id;

#ifdef ENABLE_CFG
    // blocks contained in this graph
    BBlocksMapT _blocks;

    // if we want to keep CFG information in the dependence graph,
    // these are entry and exit basic blocks
    BBlock<NodeT> *entryBB;
    BBlock<NodeT> *exitBB;

    // root of post-dominator tree
    BBlock<NodeT> *PDTreeRoot;
#endif // ENABLE_CFG

protected:
    // nodes contained in this dg. They are protected, so that
    // child classes can access them directly
    ContainerType nodes;
    // container that can be shared accross the graphs
    // (therefore it is a pointer)
    std::shared_ptr<ContainerType> global_nodes;

public:
    DependenceGraph<NodeT>()
        : entryNode(nullptr), exitNode(nullptr), formalParameters(nullptr),
          refcount(1), slice_id(0)
#ifdef ENABLE_CFG
        , entryBB(nullptr), exitBB(nullptr), PDTreeRoot(nullptr)
#endif
    {
    }

    virtual ~DependenceGraph<NodeT>() {
#ifdef ENABLE_CFG
#ifdef ENABLE_DEBUG
        bool deleted_entry = false;
        bool deleted_exit = false;
#endif // ENABLE_DEBUG
        for (auto& it : _blocks) {
#ifdef ENABLE_DEBUG
            if (it.second == entryBB)
                deleted_entry = true;
            else if (it.second == exitBB);
                deleted_exit = true;
#endif // ENABLE_DEBUG
            delete it.second;
        }

#ifdef ENABLE_DEBUG
        assert(deleted_entry && "Did not have entry in _blocks");
        assert(deleted_exit && "Did not have exit in _blocks");
#endif // ENABLE_DEBUG
#endif
    }

    // iterators for local nodes
    iterator begin(void) { return nodes.begin(); }
    const_iterator begin(void) const { return nodes.begin(); }
    iterator end(void) { return nodes.end(); }
    const_iterator end(void) const { return nodes.end(); }

    // operator [] for local nodes
    NodeT *operator[](KeyT k) { return nodes[k]; }
    const NodeT *operator[](KeyT k) const { return nodes[k]; }

    // reference getter for fast include-if-null operation
    NodeT *& getRef(KeyT k) { return nodes[k]; }

    // do we have a local node with this key?
    bool contains(KeyT k) const { return nodes.count(k) != 0; }

    // get iterator to a local node with key 'k'. If there is no
    // such a node, return end()
    iterator find(KeyT k) { return nodes.find(k); }
    const_iterator find(KeyT k) const { return nodes.find(k); }

    // get formal parameters of this graph
    DGParameters<NodeT> *getParameters() { return formalParameters;}
    DGParameters<NodeT> *getParameters() const { return formalParameters;}

    // set new parameters of this graph.
    void setParameters(DGParameters<NodeT> *p)
    {
        assert(!formalParameters && "Already have formal parameters");
        formalParameters = p;
    }

    // Get node from graph for key. The function searches in nodes,
    // formal parameters and global nodes (in this order)
    // Return nullptr if no such node exists
    template <typename T>
    NodeT *_getNode(T k)
    {
        auto it = nodes.find(k);
        if (it != nodes.end())
            return it->second;

        if (formalParameters) {
            auto p = formalParameters->find(k);
            if (p)
                return p->in;
        }

        return getGlobalNode(k);
    }

    NodeT *getNode(KeyT k) { return _getNode(k); }
    const NodeT *getNode(const KeyT k) const { return _getNode(k); }

    // get global node with given key or null if there's
    // not such node
    template <typename T>
    NodeT *_getGlobalNode(T k)
    {
        if (global_nodes) {
            auto it = global_nodes->find(k);
            if (it != global_nodes->end())
                return it->second;
        }

        return nullptr;
    }

    NodeT *getGlobalNode(KeyT k) { return _getGlobalNode(k); }
    const NodeT *getGlobalNode(const KeyT k) const { return _getGlobalNode(k); }

    // number of local nodes
    size_t size() const
    {
        return nodes.size();
    }

    NodeT *setEntry(NodeT *n)
    {
        NodeT *oldEnt = entryNode;
        entryNode = n;

        return oldEnt;
    }

    NodeT *setExit(NodeT *n)
    {
        NodeT *oldExt = exitNode;
        exitNode = n;

        return oldExt;
    }

    NodeT *getEntry(void) const { return entryNode; }
    NodeT *getExit(void) const { return exitNode; }

    // dependence graph can be shared between more call-sites that
    // has references to this graph. When destroying graph, we
    // must be sure do delete it just once, so count references
    // This is up to concrete DG implementation if it uses
    // ref()/unref() methods or handle these stuff some other way
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

        assert(refcount >= 0 && "Negative refcount");
        return refcount;
    }

    void setGlobalNodes(const std::shared_ptr<ContainerType>& ngn)
    {
        global_nodes = ngn;
    }

    // allocate new global nodes
    void allocateGlobalNodes()
    {
        assert(!global_nodes && "Already contains global nodes");
        // std::make_shared returned unaligned pointer for some reason...
        global_nodes = std::shared_ptr<ContainerType>(new ContainerType());
    }

    ContainerType *getNodes()
    {
        return &nodes;
    }

    const ContainerType *getNodes() const
    {
        return &nodes;
    }

    std::shared_ptr<ContainerType> getGlobalNodes()
    {
        return global_nodes;
    }

    const std::shared_ptr<ContainerType>& getGlobalNodes() const
    {
        return global_nodes;
    }

    // add a node to this graph. The DependenceGraph is something like
    // a namespace for nodes, since every node has unique key and we can
    // have another node with same key in another graph.
    // So we can have two nodes for the same value but in different
    // graphs. The edges can be between arbitrary nodes and do not
    // depend on graphs the nodes are in.
    bool addNode(KeyT k, NodeT *n)
    {
        bool ret = nodes.insert(std::make_pair(k, n)).second;
        if (ret) {
            assert(n->getDG() == nullptr && "A node can not belong to more graphs");
            n->setDG(static_cast<DependenceGraphT *>(this));
        }

        return ret;
    }

    // make it virtual? We don't need it now, but
    // in the future it may be handy.
    bool addNode(NodeT *n)
    {
        // NodeT is a class derived from
        // dg::Node, so it must have getKey() method
        return addNode(n->getKey(), n);
    }

    bool addGlobalNode(KeyT k, NodeT *n)
    {
        assert(global_nodes && "Need a container for global nodes first");
        return global_nodes->insert(std::make_pair(k, n)).second;
    }

    bool addGlobalNode(NodeT *n)
    {
        return addGlobalNode(n->getKey(), n);
    }

    NodeT *removeNode(KeyT k)
    {
        return _removeNode(k, &nodes);
    }

    NodeT *removeNode(NodeT *n)
    {
        return removeNode(n->getKey());
    }

    NodeT *removeNode(iterator& it)
    {
        return _removeNode(it, &nodes);
    }

    NodeT *removeGlobalNode(KeyT k)
    {
        if (!global_nodes)
            return nullptr;

        return _removeNode(k, global_nodes);
    }

    NodeT *removeGlobalNode(NodeT *n)
    {
        return removeGlobalNode(n->getKey());
    }

    NodeT *removeGlobalNode(iterator& it)
    {
        if (!global_nodes)
            return nullptr;

        return _removeNode(it, global_nodes);
    }

    bool deleteNode(NodeT *n)
    {
        return deleteNode(n->getKey());
    }

    bool deleteNode(KeyT k)
    {
        NodeT *n = removeNode(k);
        delete n;

        return n != nullptr;
    }

    bool deleteNode(iterator& it)
    {
        NodeT *n = removeNode(it);
        delete n;

        return n != nullptr;
    }

    bool deleteGlobalNode(KeyT k)
    {
        NodeT *n = removeGlobalNode(k);
        delete n;

        return n != nullptr;
    }

    bool deleteGlobalNode(NodeT *n)
    {
        return deleteGlobalNode(n->getKey());
    }

    bool deleteGlobalNode(iterator& it)
    {
        NodeT *n = removeGlobalNode(it);
        delete n;

        return n != nullptr;
    }

    DGContainer<NodeT *>& getCallers() { return callers; }
    const DGContainer<NodeT *>& getCallers() const { return callers; }
    bool addCaller(NodeT *sg) { return callers.insert(sg); }

    // set that this graph (if it is subgraph)
    // will be left in a slice. It is virtual, because the graph
    // may want to override the function and take some action,
    // if it is in a graph
    virtual void setSlice(uint64_t sid)
    {
        slice_id = sid;
    }

    uint64_t getSlice() const { return slice_id; }

#ifdef ENABLE_CFG
    // get blocks contained in this graph
    BBlocksMapT& getBlocks() { return _blocks; }
    const BBlocksMapT& getBlocks() const { return _blocks; }
    // add block to this graph
    bool addBlock(KeyT key, BBlock<NodeT> *B) {
        return _blocks.emplace(key, B).second;
    }

    bool removeBlock(KeyT key)
    {
        return _blocks.erase(key) == 1;
    }

    BBlock<NodeT> *getPostDominatorTreeRoot() const { return PDTreeRoot; }
    void setPostDominatorTreeRoot(BBlock<NodeT> *r)
    {
        assert(!PDTreeRoot && "Already has a post-dominator tree root");
        PDTreeRoot = r;
    }

    BBlock<NodeT> *getEntryBB() const { return entryBB; }
    BBlock<NodeT> *getExitBB() const { return exitBB; }

    BBlock<NodeT> *setEntryBB(BBlock<NodeT> *nbb)
    {
        BBlock<NodeT> *old = entryBB;
        entryBB = nbb;

        return old;
    }

    BBlock<NodeT> *setExitBB(BBlock<NodeT> *nbb)
    {
        BBlock<NodeT> *old = exitBB;
        exitBB = nbb;

        return old;
    }

#endif // ENABLE_CFG

private:

    NodeT *_removeNode(iterator& it, ContainerType *cont)
    {
        NodeT *n = it->second;
        n->isolate();
        cont->erase(it);

        return n;
    }

    NodeT *_removeNode(KeyT k, ContainerType *cont)
    {
        iterator it = cont->find(k);
        if (it == cont->end())
            return nullptr;

        // remove and re-connect edges
        return _removeNode(it, cont);
    }
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
