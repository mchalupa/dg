/// XXX add licence
//

#ifndef _NODE_H_
#define _NODE_H_

#include "DGParameters.h"
#include "analysis/Analysis.h"

namespace dg {

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
    typedef typename DependenceEdgesT::iterator data_iterator;
    typedef typename DependenceEdgesT::const_iterator const_data_iterator;

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

    DGParameters<KeyT, NodePtrT> *
    addParameters(DGParameters<KeyT, NodePtrT> *params)
    {
        DGParameters<KeyT, NodePtrT> *old = parameters;

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

    DGParameters<KeyT, NodePtrT> *getParameters() const
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

    // actual parameters if this is a callsite
    DGParameters<KeyT, NodePtrT> *parameters;

    // auxiliary data for different analyses
    analysis::AnalysesAuxiliaryData analysisAuxData;
    friend class analysis::Analysis<NodePtrT>;
};

} // namespace dgg

#endif // _NODE_H_
