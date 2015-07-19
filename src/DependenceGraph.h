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
template <typename Key, typename ValueType>
class DependenceGraph
{
public:
    typedef std::map<Key, ValueType> ContainerType;
    typedef typename ContainerType::iterator iterator;
    typedef typename ContainerType::const_iterator const_iterator;

    DependenceGraph<Key, ValueType>()
    :entryNode(nullptr), exitNode(nullptr), refcount(1)
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
    bool contains(Key k) const { return nodes.count(k) != 0; }
    iterator find(Key k) { return nodes.find(k); }
    const_iterator find(Key k) const { return nodes.find(k); }

    ///
    // Get node from graph for key. Return nullptr if
    // no such node exists
    ValueType getNode(Key k)
    {
        iterator it = nodes.find(k);
        if (it == nodes.end())
            return nullptr;

        return it->second;
    }

    size_t size() const
    {
        return nodes.size();
    }

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

        assert(refcount >= 0 && "Negative refcount");
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
    bool addNode(Key k, ValueType n)
    {
        return nodes.insert(std::make_pair(k, n)).second;
    }

    // make it virtual? We don't need it now, but
    // in the future it may be handy.
    bool addNode(ValueType n)
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

protected:
    // nodes contained in this dg. They are protected, so that
    // child classes can access them directly
    ContainerType nodes;

private:
    ValueType entryNode;
    ValueType exitNode;

#ifdef ENABLE_CFG
    BBlock<ValueType> *entryBB;
    BBlock<ValueType> *exitBB;
#endif // ENABLE_CFG

    // how many nodes keeps pointer to this graph?
    int refcount;
};

} // namespace dg

#endif // _DEPENDENCE_GRAPH_H_
