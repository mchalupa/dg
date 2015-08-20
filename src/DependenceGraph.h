/// XXX add licence
//

#ifndef _DEPENDENCE_GRAPH_H_
#define _DEPENDENCE_GRAPH_H_

#include <utility>
#include <queue>
#include <map>
#include <cassert>

#include "Utils.h" // DBG macro
#include "BBlock.h"
#include "EdgesContainer.h"
#include "Node.h"

#include "analysis/Analysis.h"

namespace dg {

// --------------------------------------------------------
// --- DependenceGraph
// --------------------------------------------------------
template <typename NodeT>
class DependenceGraph
{
public:
    typedef typename NodeT::KeyType KeyT;
    typedef std::map<KeyT, NodeT *> ContainerType;
    typedef typename ContainerType::iterator iterator;
    typedef typename ContainerType::const_iterator const_iterator;

    DependenceGraph<NodeT>()
        : global_nodes(nullptr), entryNode(nullptr), exitNode(nullptr),
          formalParameters(nullptr), refcount(1), own_global_nodes(false)
#ifdef ENABLE_CFG
     , entryBB(nullptr), exitBB(nullptr)
#endif
    {
    }

    // TODO add copy constructor for cloning graph

    virtual ~DependenceGraph<NodeT>()
    {
        if (own_global_nodes)
            delete global_nodes;
    }

    // iterators
    iterator begin(void) { return nodes.begin(); }
    const_iterator begin(void) const { return nodes.begin(); }
    iterator end(void) { return nodes.end(); }
    const_iterator end(void) const { return nodes.end(); }

    NodeT *operator[](KeyT k) { return nodes[k]; }
    const NodeT *operator[](KeyT k) const { return nodes[k]; }
    // reference getter for fast include-if-null operation
    NodeT *& getRef(KeyT k) { return nodes[k]; }
    bool contains(KeyT k) const { return nodes.count(k) != 0; }
    iterator find(KeyT k) { return nodes.find(k); }
    const_iterator find(KeyT k) const { return nodes.find(k); }

    DGParameters<KeyT, NodeT> *getParameters() { return formalParameters;}
    DGParameters<KeyT, NodeT> *getParameters() const { return formalParameters;}
    DGParameters<KeyT, NodeT> *setParameters(DGParameters<KeyT, NodeT> *p)
    {
        DGParameters<KeyT, NodeT> *old = formalParameters;
        formalParameters = p;
        return old;
    }

    ///
    // Get node from graph for key. Return nullptr if
    // no such node exists
    NodeT *getNode(KeyT k)
    {
        iterator it = nodes.find(k);
        if (it != nodes.end())
            return it->second;
        else if (global_nodes) {
            it = global_nodes->find(k);
            if (it == global_nodes->end())
                return it->second;
        }

        return nullptr;
    }

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

        assert(refcount >= 0 && "Negative refcount");
        return refcount;
    }

#ifdef ENABLE_CFG
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
    ContainerType *setGlobalNodes(ContainerType *ngn)
    {
        ContainerType *old = global_nodes;
        global_nodes = ngn;
        return old;
    }

    // allocate new global nodes
    ContainerType *createGlobalNodes()
    {
        assert(!global_nodes && "Already contain global nodes");
        global_nodes = new ContainerType();

        return global_nodes;
    }

    ContainerType *getGlobalNodes()
    {
        return global_nodes;
    }

    const ContainerType *getGlobalNodes() const
    {
        return global_nodes;
    }

    // add a node to this graph. The DependenceGraph is something like
    // namespace for nodes, since every node has unique key and we can
    // have another node with same key in another
    // graph. So we can have two nodes for the same value but in different
    // graphs. The edges can be between arbitrary nodes and do not
    // depend on graphs the nodes are in.
    bool addNode(KeyT k, NodeT *n)
    {
        bool ret = nodes.insert(std::make_pair(k, n)).second;
        if (ret)
            n->setDG(static_cast<typename NodeT::DependenceGraphType *>(this));

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
        if (!global_nodes) {
            global_nodes = new ContainerType();
            own_global_nodes = true;
        }

        bool ret = global_nodes->insert(std::make_pair(k, n)).second;
        if (ret && own_global_nodes)
            n->setDG(static_cast<typename NodeT::DependenceGraphType *>(this));

        return ret;
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

    bool deleteNode(KeyT k)
    {
        NodeT *n = removeNode(k);
        delete n;

        return n != nullptr;
    }

    bool deleteGlobalNode(KeyT k)
    {
        NodeT *n = removeGlobalNode(k);
        delete n;

        return n != nullptr;
    }

protected:
    // nodes contained in this dg. They are protected, so that
    // child classes can access them directly
    ContainerType nodes;
    // container that can be shared accross the graphs
    // (therefore it is a pointer)
    ContainerType *global_nodes;

private:
    NodeT *_removeNode(KeyT k, ContainerType *cont)
    {
        iterator it = cont->find(k);
        if (it == cont->end())
            return nullptr;

        // remove and re-connect edges
        NodeT *n = it->second;
        n->isolate();

        cont->erase(it);

        return n;
    }

    NodeT *entryNode;
    NodeT *exitNode;

    DGParameters<KeyT, NodeT> *formalParameters;

    // how many nodes keeps pointer to this graph?
    int refcount;
    bool own_global_nodes;

#ifdef ENABLE_CFG
    BBlock<NodeT> *entryBB;
    BBlock<NodeT> *exitBB;
#endif // ENABLE_CFG
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
