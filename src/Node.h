/// XXX add licence
//

#ifndef _NODE_H_
#define _NODE_H_

#include "DGParameters.h"
#include "ADT/DGContainer.h"
#include "analysis/Analysis.h"

namespace dg {

template <typename NodeT>
class DependenceGraph;

/// ------------------------------------------------------------------
//  -- Node
//     one node in DependenceGraph. The type of dependence graph is
//     fully determined by the type of node. Dependence graph is just
//     a container for nodes - everything interesting is here.
//     Concrete implementation will inherit from an instance of this
//     template.
/// ------------------------------------------------------------------
template <typename DependenceGraphT, typename KeyT, typename NodeT>
class Node
{
public:
    using ControlEdgesT = EdgesContainer<NodeT>;
    using DependenceEdgesT = EdgesContainer<NodeT>;

    // to be able to reference the KeyT and DG
    using KeyType = KeyT;
    using DependenceGraphType = DependenceGraphT;

    using control_iterator = typename ControlEdgesT::iterator;
    using const_control_iterator = typename ControlEdgesT::const_iterator;
    using data_iterator = typename DependenceEdgesT::iterator;
    using const_data_iterator = typename DependenceEdgesT::const_iterator;

    Node<DependenceGraphT, KeyT, NodeT>(const KeyT& k,
                                        DependenceGraphT *dg = nullptr)
        : key(k), dg(dg), parameters(nullptr), slice_id(0)
#ifdef ENABLE_CFG
         , basicBlock(nullptr)
#endif
    {
        if (dg)
            dg->addNode(static_cast<NodeT *>(this));
    }

    // remove this node from dg (from the container - the memory is still valid
    // and must be freed later)
    void removeFromDG()
    {
        dg->removeNode(key);
    }

    DependenceGraphT *setDG(DependenceGraphT *dg)
    {
        DependenceGraphT *old = this->dg;
        this->dg = dg;

        return old;
    }

    DependenceGraphT *getDG() const
    {
        return dg;
    }

    // add control dependence edge 'this'-->'n',
    // thus making 'n' control dependend on this node
    bool addControlDependence(NodeT * n)
    {
#ifndef NDEBUG
        bool ret1;
#endif
        bool ret2;

#ifndef NDEBUG
        ret1 =
#endif
        n->revControlDepEdges.insert(static_cast<NodeT *>(this));
        ret2 = controlDepEdges.insert(n);

        // we either have both edges or none
        assert(ret1 == ret2);

        return ret2;
    }

    // add data dependence edge 'this'-->'n',
    // thus making 'n' data dependend on this node
    bool addDataDependence(NodeT * n)
    {
#ifndef NDEBUG
        bool ret1;
#endif
        bool ret2;


#ifndef NDEBUG
        ret1 =
#endif
        n->revDataDepEdges.insert(static_cast<NodeT *>(this));
        ret2 = dataDepEdges.insert(n);

        assert(ret1 == ret2);

        return ret2;
    }

    // remove edge 'this'-->'n' from data dependencies
    bool removeDataDependence(NodeT * n)
    {
        bool ret1;
#ifndef NDEBUG
        bool ret2;
#endif
        ret1 = n->revDataDepEdges.erase(static_cast<NodeT *>(this));
#ifndef NDEBUG
        ret2 =
#endif
        dataDepEdges.erase(n);

        // must have both or none
        assert(ret1 == ret2 && "Dep. edge without rev. or vice versa");

        return ret1;
    }

    // remove edge 'this'-->'n' from control dependencies
    bool removeControlDependence(NodeT * n)
    {
        bool ret1;
#ifndef NDEBUG
        bool ret2;
#endif

        ret1 = n->revControlDepEdges.erase(static_cast<NodeT *>(this));
#ifndef NDEBUG
        ret2 =
#endif
        controlDepEdges.erase(n);

        // must have both or none
        assert(ret1 == ret2 && "Control edge without rev. or vice versa");

        return ret1;
    }

    void removeOutcomingDDs()
    {
        while (!dataDepEdges.empty())
            removeDataDependence(*dataDepEdges.begin());
    }

    void removeIncomingDDs()
    {
        while (!revDataDepEdges.empty()) {
            NodeT *cd = *revDataDepEdges.begin();
            // this will remove the reverse control dependence from
            // this node
            cd->removeDataDependence(static_cast<NodeT *>(this));
        }
    }

    // remove all data dependencies going from/to this node
    void removeDDs()
    {
        removeOutcomingDDs();
        removeIncomingDDs();
    }

    // remove all control dependencies going from/to this node
    void removeOutcomingCDs()
    {
        while (!controlDepEdges.empty())
            removeControlDependence(*controlDepEdges.begin());
    }

    void removeIncomingCDs()
    {
        while (!revControlDepEdges.empty()) {
            NodeT *cd = *revControlDepEdges.begin();
            // this will remove the reverse control dependence from
            // this node
            cd->removeControlDependence(static_cast<NodeT *>(this));
        }
    }

    void removeCDs()
    {
        removeOutcomingCDs();
        removeIncomingCDs();
    }

    // remove all edges from/to this node
    void isolate()
    {
        // remove CD and DD from this node
        removeDDs();
        removeCDs();

#ifdef ENABLE_CFG
        // if this is head or tail of BB,
        // we must take it into account
        if (basicBlock) {
            // XXX removing the node from BB is in linear time,
            // could we do it better?
            basicBlock->removeNode(static_cast<NodeT *>(this));

            // if this was the only node in BB, remove the BB
            if (basicBlock->empty())
                basicBlock->remove();

            // if this is a callSite it is no longer part of BBlock,
            // so we must remove it from callSites
            if (hasSubgraphs()) {
#ifndef NDEBUG
                bool ret =
#endif
                basicBlock->removeCallSite(static_cast<NodeT *>(this));
                assert(ret && "the call site was not in BB's callSites");
            }

            basicBlock = nullptr;
        }
#endif
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
    data_iterator data_begin(void) { return dataDepEdges.begin(); }
    const_data_iterator data_begin(void) const { return dataDepEdges.begin(); }
    data_iterator data_end(void) { return dataDepEdges.end(); }
    const_data_iterator data_end(void) const { return dataDepEdges.end(); }

    // reverse data dependency edges iterators
    data_iterator rev_data_begin(void) { return revDataDepEdges.begin(); }
    const_data_iterator rev_data_begin(void) const { return revDataDepEdges.begin(); }
    data_iterator rev_data_end(void) { return revDataDepEdges.end(); }
    const_data_iterator rev_data_end(void) const { return revDataDepEdges.end(); }

    unsigned int getControlDependenciesNum() const { return controlDepEdges.size(); }
    unsigned int getRevControlDependenciesNum() const { return revControlDepEdges.size(); }
    unsigned int getDataDependenciesNum() const { return dataDepEdges.size(); }
    unsigned int getRevDataDependenciesNum() const { return revDataDepEdges.size(); }

#ifdef ENABLE_CFG
    BBlock<NodeT> *getBBlock() { return basicBlock; }
    const BBlock<NodeT> *getBBlock() const { return basicBlock; }

    BBlock<NodeT> *setBasicBlock(BBlock<NodeT> *nbb)
    {
        BBlock<NodeT> *old = basicBlock;
        basicBlock = nbb;
        return old;
    }

    unsigned int getDFSOrder() const
    {
        return analysisAuxData.dfsorder;
    }

#endif /* ENABLE_CFG */

    bool addSubgraph(DependenceGraphT *sub)
    {
        bool ret = subgraphs.insert(sub).second;

        if (ret) {
            // increase references of this graph
            // if we added it
            sub->ref();
            sub->addCaller(static_cast<NodeT *>(this));
        }

        return ret;
    }

    DGParameters<NodeT> *
    setParameters(DGParameters<NodeT> *params)
    {
        DGParameters<NodeT> *old = parameters;

        parameters = params;
        return old;
    }

    const std::set<DependenceGraphT *>& getSubgraphs(void) const
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

    DGParameters<NodeT> *getParameters() const
    {
        return parameters;
    }

    KeyT getKey() const
    {
        return key;
    }

    uint32_t getSlice() const { return slice_id; }
    uint32_t setSlice(uint32_t sid)
    {
        uint32_t old = slice_id;
        slice_id = sid;
        return old;
    }

protected:

    // key uniquely identifying this node in a graph
    KeyT key;

    // each node has a reference to the DependenceGraph
    DependenceGraphT *dg;

private:
    ControlEdgesT controlDepEdges;
    DependenceEdgesT dataDepEdges;

    // Nodes that have control/dep edge to this node
    ControlEdgesT revControlDepEdges;
    DependenceEdgesT revDataDepEdges;

    // a node can have more subgraphs (i. e. function pointers)
    std::set<DependenceGraphT *> subgraphs;

    // actual parameters if this is a callsite
    DGParameters<NodeT> *parameters;

    // id of the slice this nodes is in. If it is 0, it is in no slice
    uint32_t slice_id;

#ifdef ENABLE_CFG
    // some analyses need classical CFG edges
    // and it is better to have even basic blocks
    BBlock<NodeT> *basicBlock;
#endif /* ENABLE_CFG */

    // auxiliary data for different analyses
    analysis::AnalysesAuxiliaryData analysisAuxData;
    friend class analysis::Analysis<NodeT>;
};

} // namespace dg

#endif // _NODE_H_
