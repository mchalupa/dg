#ifndef NODE_H_
#define NODE_H_

#include "ADT/DGContainer.h"
#include "DGParameters.h"
#include "legacy/Analysis.h"

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
class Node {
  public:
    using EdgesT = EdgesContainer<NodeT>;
    using ControlEdgesT = EdgesT;
    using DataEdgesT = EdgesT;
    using UseEdgesT = EdgesT;
    using InterferenceEdges = EdgesT;

    // to be able to reference the KeyT and DG
    using KeyType = KeyT;
    using DependenceGraphType = DependenceGraphT;

    using control_iterator = typename ControlEdgesT::iterator;
    using const_control_iterator = typename ControlEdgesT::const_iterator;
    using data_iterator = typename DataEdgesT::iterator;
    using const_data_iterator = typename DataEdgesT::const_iterator;
    using use_iterator = typename DataEdgesT::iterator;
    using const_use_iterator = typename DataEdgesT::const_iterator;
    using interference_iterator = typename InterferenceEdges::iterator;
    using const_interference_iterator =
            typename InterferenceEdges::const_iterator;

    Node(const KeyT &k) : key(k) {}

    DependenceGraphT *setDG(DependenceGraphT *dg) {
        DependenceGraphT *old = this->dg;
        this->dg = dg;

        return old;
    }

    DependenceGraphT *getDG() const { return dg; }

    // add control dependence edge 'this'-->'n',
    // thus making 'n' control dependend on this node
    bool addControlDependence(NodeT *n) {
        return _addBidirectionalEdge(static_cast<NodeT *>(this), n,
                                     controlDepEdges, n->revControlDepEdges);
    }

    // add data dependence edge 'this'-->'n',
    // thus making 'n' data dependend on this node
    bool addDataDependence(NodeT *n) {
        return _addBidirectionalEdge(static_cast<NodeT *>(this), n,
                                     dataDepEdges, n->revDataDepEdges);
    }

    // this node uses (e.g. like an operand) the node 'n'
    bool addUseDependence(NodeT *n) {
        return _addBidirectionalEdge(static_cast<NodeT *>(this), n, useEdges,
                                     n->userEdges);
    }

    bool addInterferenceDependence(NodeT *n) {
        return _addBidirectionalEdge(static_cast<NodeT *>(this), n,
                                     interferenceDepEdges,
                                     n->revInterferenceDepEdges);
    }

    // remove edge 'this'-->'n' from control dependencies
    bool removeControlDependence(NodeT *n) {
        return _removeBidirectionalEdge(static_cast<NodeT *>(this), n,
                                        controlDepEdges, n->revControlDepEdges);
    }

    // remove edge 'this'-->'n' from data dependencies
    bool removeDataDependence(NodeT *n) {
        return _removeBidirectionalEdge(static_cast<NodeT *>(this), n,
                                        dataDepEdges, n->revDataDepEdges);
    }

    bool removeUseDependence(NodeT *n) {
        return _removeBidirectionalEdge(static_cast<NodeT *>(this), n, useEdges,
                                        n->userEdges);
    }

    bool removeInterferenceDependence(NodeT *n) {
        return _removeBidirectionalEdge(static_cast<NodeT *>(this), n,
                                        interferenceDepEdges,
                                        n->revInterferenceDepEdges);
    }

    // remove all control dependencies going from/to this node
    void removeOutcomingCDs() {
        while (!controlDepEdges.empty())
            removeControlDependence(*controlDepEdges.begin());
    }

    void removeIncomingCDs() {
        while (!revControlDepEdges.empty()) {
            NodeT *cd = *revControlDepEdges.begin();
            // this will remove the reverse control dependence from
            // this node
            cd->removeControlDependence(static_cast<NodeT *>(this));
        }
    }

    void removeCDs() {
        removeOutcomingCDs();
        removeIncomingCDs();
    }

    void removeOutcomingDDs() {
        while (!dataDepEdges.empty())
            removeDataDependence(*dataDepEdges.begin());
    }

    void removeIncomingDDs() {
        while (!revDataDepEdges.empty()) {
            NodeT *cd = *revDataDepEdges.begin();
            // this will remove the reverse control dependence from
            // this node
            cd->removeDataDependence(static_cast<NodeT *>(this));
        }
    }

    // remove all data dependencies going from/to this node
    void removeDDs() {
        removeOutcomingDDs();
        removeIncomingDDs();
    }

    void removeOutcomingUses() {
        while (!useEdges.empty())
            removeUseDependence(*useEdges.begin());
    }

    void removeIncomingUses() {
        while (!userEdges.empty()) {
            NodeT *cd = *userEdges.begin();
            // this will remove the reverse control dependence from
            // this node
            cd->removeUseDependence(static_cast<NodeT *>(this));
        }
    }

    // remove all direct (top-level) data dependencies going from/to this node
    void removeUses() {
        removeOutcomingUses();
        removeIncomingUses();
    }

    // remove all edges from/to this node
    void isolate() {
        // remove CD and DD and uses from this node
        removeDDs();
        removeUses();
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
    control_iterator control_begin() { return controlDepEdges.begin(); }
    const_control_iterator control_begin() const {
        return controlDepEdges.begin();
    }
    control_iterator control_end() { return controlDepEdges.end(); }
    const_control_iterator control_end() const { return controlDepEdges.end(); }

    // reverse control dependency edges iterators
    control_iterator rev_control_begin() { return revControlDepEdges.begin(); }
    const_control_iterator rev_control_begin() const {
        return revControlDepEdges.begin();
    }
    control_iterator rev_control_end() { return revControlDepEdges.end(); }
    const_control_iterator rev_control_end() const {
        return revControlDepEdges.end();
    }

    // interference dependency edges iteraotrs
    const_interference_iterator interference_begin() const {
        return interferenceDepEdges.begin();
    }
    interference_iterator interference_begin() {
        return interferenceDepEdges.begin();
    }
    const_interference_iterator interference_end() const {
        return interferenceDepEdges.end();
    }
    interference_iterator interference_end() {
        return interferenceDepEdges.end();
    }

    // reverse interference dependency edges iterators
    const_interference_iterator rev_interference_begin() const {
        return revInterferenceDepEdges.begin();
    }
    interference_iterator rev_interference_begin() {
        return revInterferenceDepEdges.begin();
    }
    const_interference_iterator rev_interference_end() const {
        return revInterferenceDepEdges.end();
    }
    interference_iterator rev_interference_end() {
        return revInterferenceDepEdges.end();
    }

    /// NOTE: we have two kinds of data dependencies.
    // The first one is when a value is used as an argument
    // in another instruction, that is direct (or top-level) dependency.
    // The other case is when an instruction reads a value from memory
    // which has been written by another instruction. This is
    // "indirect" dependency.
    // The user can choose whether to use both or just one of
    // these dependencies.

    // data dependency edges iterators (indirect dependency)
    data_iterator data_begin() { return dataDepEdges.begin(); }
    const_data_iterator data_begin() const { return dataDepEdges.begin(); }
    data_iterator data_end() { return dataDepEdges.end(); }
    const_data_iterator data_end() const { return dataDepEdges.end(); }

    // reverse data dependency edges iterators (indirect dependency)
    data_iterator rev_data_begin() { return revDataDepEdges.begin(); }
    const_data_iterator rev_data_begin() const {
        return revDataDepEdges.begin();
    }
    data_iterator rev_data_end() { return revDataDepEdges.end(); }
    const_data_iterator rev_data_end() const { return revDataDepEdges.end(); }

    // use dependency edges iterators (indirect data dependency
    // -- uses of this node e.g. in operands)
    use_iterator use_begin() { return useEdges.begin(); }
    const_use_iterator use_begin() const { return useEdges.begin(); }
    use_iterator use_end() { return useEdges.end(); }
    const_use_iterator use_end() const { return useEdges.end(); }

    // user dependency edges iterators (indirect data dependency)
    use_iterator user_begin() { return userEdges.begin(); }
    const_use_iterator user_begin() const { return userEdges.begin(); }
    use_iterator user_end() { return userEdges.end(); }
    const_use_iterator user_end() const { return userEdges.end(); }

    size_t getControlDependenciesNum() const { return controlDepEdges.size(); }
    size_t getRevControlDependenciesNum() const {
        return revControlDepEdges.size();
    }
    size_t getDataDependenciesNum() const { return dataDepEdges.size(); }
    size_t getRevDataDependenciesNum() const { return revDataDepEdges.size(); }
    size_t getUseDependenciesNum() const { return useEdges.size(); }
    size_t getUserDependenciesNum() const { return userEdges.size(); }

#ifdef ENABLE_CFG
    BBlock<NodeT> *getBBlock() { return basicBlock; }
    const BBlock<NodeT> *getBBlock() const { return basicBlock; }

    BBlock<NodeT> *setBasicBlock(BBlock<NodeT> *nbb) {
        BBlock<NodeT> *old = basicBlock;
        basicBlock = nbb;
        return old;
    }

    unsigned int getDFSOrder() const { return analysisAuxData.dfsorder; }

#endif /* ENABLE_CFG */

    bool addSubgraph(DependenceGraphT *sub) {
        bool ret = subgraphs.insert(sub).second;

        if (ret) {
            // increase references of this graph
            // if we added it
            sub->ref();
            sub->addCaller(static_cast<NodeT *>(this));
        }

        return ret;
    }

    DGParameters<NodeT> *setParameters(DGParameters<NodeT> *params) {
        DGParameters<NodeT> *old = parameters;

        parameters = params;
        return old;
    }

    const std::set<DependenceGraphT *> &getSubgraphs() const {
        return subgraphs;
    }

    bool hasSubgraphs() const { return !subgraphs.empty(); }

    size_t subgraphsNum() const { return subgraphs.size(); }

    DGParameters<NodeT> *getParameters() const { return parameters; }

    KeyT getKey() const { return key; }

    uint32_t getSlice() const { return slice_id; }
    uint32_t setSlice(uint32_t sid) {
        uint32_t old = slice_id;
        slice_id = sid;
        return old;
    }

  protected:
    // key uniquely identifying this node in a graph
    KeyT key{};

    // each node has a reference to the DependenceGraph
    DependenceGraphT *dg{nullptr};

  private:
    // add an edge 'ths' --> 'n' to containers of 'ths' and 'n'
    static bool _addBidirectionalEdge(NodeT *ths, NodeT *n, EdgesT &ths_cont,
                                      EdgesT &n_cont) {
#ifndef NDEBUG
        bool ret1 =
#endif
                n_cont.insert(ths);
        bool ret2 = ths_cont.insert(n);

        assert(ret1 == ret2 &&
               "Already had one of the edges, but not the other");

        return ret2;
    }

    // remove edge 'this'-->'n' from control dependencies
    static bool _removeBidirectionalEdge(NodeT *ths, NodeT *n, EdgesT &ths_cont,
                                         EdgesT &n_cont) {
        bool ret1 = n_cont.erase(ths);
#ifndef NDEBUG
        bool ret2 =
#endif
                ths_cont.erase(n);

        // must have both or none
        assert(ret1 == ret2 && "An edge without rev. or vice versa");

        return ret1;
    }

    ControlEdgesT controlDepEdges;
    DataEdgesT dataDepEdges;
    UseEdgesT useEdges;
    InterferenceEdges interferenceDepEdges;

    // Nodes that have control/dep edge to this node
    ControlEdgesT revControlDepEdges;
    DataEdgesT revDataDepEdges;
    UseEdgesT userEdges;
    InterferenceEdges revInterferenceDepEdges;

    // a node can have more subgraphs (i. e. function pointers)
    std::set<DependenceGraphT *> subgraphs;

    // actual parameters if this is a callsite
    DGParameters<NodeT> *parameters{nullptr};

    // id of the slice this nodes is in. If it is 0, it is in no slice
    uint32_t slice_id{0};

#ifdef ENABLE_CFG
    // some analyses need classical CFG edges
    // and it is better to have even basic blocks
    BBlock<NodeT> *basicBlock{nullptr};
#endif /* ENABLE_CFG */

    // auxiliary data for different analyses
    legacy::AnalysesAuxiliaryData analysisAuxData;
    friend class legacy::Analysis<NodeT>;
};

} // namespace dg

#endif // _NODE_H_
