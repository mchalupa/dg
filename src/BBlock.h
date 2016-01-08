/// XXX add licence
//

#ifndef _BBLOCK_H_
#define _BBLOCK_H_

#include <cassert>
#include <list>

#include "ADT/DGContainer.h"
#include "analysis/Analysis.h"

#ifndef ENABLE_CFG
#error "BBlock.h needs be included with ENABLE_CFG"
#endif // ENABLE_CFG

namespace dg {

/// ------------------------------------------------------------------
// - BBlock
//     Basic block structure for dependence graph
/// ------------------------------------------------------------------
template <typename NodeT>
class BBlock
{
public:
    typedef typename NodeT::KeyType KeyT;
    typedef typename NodeT::DependenceGraphType DependenceGraphT;

    struct BBlockEdge {
        BBlockEdge(BBlock<NodeT>* t, uint8_t label = 0)
            : target(t), label(label) {}

        BBlock<NodeT> *target;
        // we'll have just numbers as labels now.
        // We can change it if there's a need
        uint8_t label;

        bool operator==(const BBlockEdge& oth) const
        {
            return target == oth.target && label == oth.label;
        }

        bool operator!=(const BBlockEdge& oth) const
        {
            return operator==(oth);
        }

        bool operator<(const BBlockEdge& oth) const
        {
            return target == oth.target ?
                        label < oth.label : target < oth.target;
        }
    };

    BBlock<NodeT>(NodeT *head = nullptr, DependenceGraphT *dg = nullptr)
        : key(KeyT()), dg(dg), ipostdom(nullptr), slice_id(0)
    {
        if (head) {
            append(head);
            assert(!dg || head->getDG() == nullptr || dg == head->getDG());
        }
    }

    typedef EdgesContainer<BBlock<NodeT>> BBlockContainerT;
    // we don't need labels with predecessors
    typedef EdgesContainer<BBlock<NodeT>> PredContainerT;
    typedef DGContainer<BBlockEdge> SuccContainerT;

    SuccContainerT& successors() { return nextBBs; }
    const SuccContainerT& successors() const { return nextBBs; }

    PredContainerT& predecessors() { return prevBBs; }
    const PredContainerT& predecessors() const { return prevBBs; }

    const BBlockContainerT& controlDependence() const { return controlDeps; }
    const BBlockContainerT& RevControlDependence() const { return revControlDeps; }

    // similary to nodes, basic blocks can have keys
    // they are not stored anywhere, it is more due to debugging
    void setKey(const KeyT& k) { key = k; }
    const KeyT& getKey() const { return key; }

    // XXX we should do it a common base with node
    // to not duplicate this - something like
    // GraphElement that would contain these attributes
    void setDG(DependenceGraphT *d) { dg = d; }
    DependenceGraphT *getDG() const { return dg; }

    const std::list<NodeT *>& getNodes() const { return nodes; }
    std::list<NodeT *>& getNodes() { return nodes; }
    bool empty() const { return nodes.empty(); }
    size_t size() const { return nodes.size(); }

    void append(NodeT *n)
    {
        assert(n && "Cannot add null node to BBlock");

        n->setBasicBlock(this);
        nodes.push_back(n);
    }

    void prepend(NodeT *n)
    {
        assert(n && "Cannot add null node to BBlock");

        n->setBasicBlock(this);
        nodes.push_front(n);
    }

    bool hasControlDependence() const
    {
        return !controlDeps.empty();
    }

    // remove all edges from/to this BB
    void isolate()
    {
        // remove this BB from predecessors
        DGContainer<uint8_t> pred_labels;

        for (auto pred : prevBBs) {
            bool found = false;
            for (auto I = pred->nextBBs.begin(),E = pred->nextBBs.end(); I != E;) {
                auto cur = I++;
                if (cur->target == this) {
                    // store labels that are going to this block,
                    // new edges will have these
                    pred_labels.insert(cur->label);
                    // remove this edge
                    pred->nextBBs.erase(*cur);
                    found = true;
                }
            }

            assert(found && "Did not have this BB as succesor of predecessor");
        }

        for (auto succ : nextBBs) {
            // redirect edges from each predecessor
            // to each successor
            for (auto pred : prevBBs) {
                for (uint8_t label : pred_labels) {
                    pred->addSuccessor(succ.target, label);
                }
            }

            bool ret = succ.target->prevBBs.erase(this);
            assert(ret && "Did not have this BB in successor's pred");
        }

        // we reconnected and deleted edges from other
        // BBs, but we still have edges from this to other BBs
        prevBBs.clear();
        nextBBs.clear();

        // remove reverse edges to this BB
        for (auto B : controlDeps)
            B->revControlDeps.erase(this);

        controlDeps.clear();
    }

    void remove(bool with_nodes = true)
    {
        // do not leave any dangling reference
        isolate();

        // XXX what to do when this is entry block?

        if (with_nodes) {
            for (NodeT *n : nodes) {
                // we must set basic block to nullptr
                // otherwise the node will try to remove the
                // basic block again if it is of size 1
                n->setBasicBlock(nullptr);

                // remove dependency edges, let be CFG edges
                // as we'll destroy all the nodes
                n->removeCDs();
                n->removeDDs();
                // remove the node from dg
                n->removeFromDG();

                delete n;
            }
        }

        delete this;
    }

    void removeNode(NodeT *n) { nodes.remove(n); }

    size_t successorsNum() const { return nextBBs.size(); }
    size_t predecessorsNum() const { return prevBBs.size(); }

    bool addSuccessor(const BBlockEdge& edge)
    {
        bool ret;

        // do not allow self-loops
        if (edge.target == this)
            return false;

        ret = nextBBs.insert(edge);
        edge.target->prevBBs.insert(this);

        return ret;
    }

    bool addSuccessor(BBlock<NodeT> *b, uint8_t label = 0)
    {
        return addSuccessor(BBlockEdge(b, label));
    }

    void removeSuccessors()
    {
        for (auto BB : nextBBs) {
            BB.target->prevBBs.erase(this);
        }

        nextBBs.clear();
    }

    void removePredecessors()
    {
        for (auto BB : prevBBs) {
            BB->nextBBs.erase(this);
        }

        prevBBs.clear();
    }

    // remove predecessor basic block. Return value is
    // 0 if nothing was removed, 1 if only one edge was removed
    // (asserted when NDEBUG is defined)
    // and two if both edges were removed.
    // (Edges are [this -> p] and [p -> this])
    //size_t removePredecessor(BBlock<NodeT> *p)
    //{
    //}

    // return value is the same as with removePredecessor
    //size_t removeSuccessor(BBlock<NodeT> *p, uint8_t label = 0)
    //{
    //    return removeSuccessor(BBlockEdge(p, label));
    //}

    //size_t removeSuccessor(BBlockEdge& edge)
    //{
    //    size_t ret = 0;
    //    ret += p->prevBBs.erase(this);
    //    ret += nextBBs.erase(edge);

    //    // return value 1 means bug
    //    assert(ret != 1 && "Bug in edges between basic blocks");

    //    return ret;
    //}

    bool addControlDependence(BBlock<NodeT> *b)
    {
        bool ret, ret2;

        // do not allow self-loops
        if (b == this)
            return false;

        ret = controlDeps.insert(b);
        ret2 = b->revControlDeps.insert(this);

        // we either have both edges or none
        assert(ret == ret2);

        return ret;
    }

    // get first node from bblock
    // or nullptr if the block is empty
    NodeT *getFirstNode() const
    {
        if (nodes.empty())
            return nullptr;

        return nodes.front();
    }

    // get last node from block
    // or nullptr if the block is empty
    NodeT *getLastNode() const
    {
        if (nodes.empty())
            return nullptr;

        return nodes.back();
    }

    // XXX: do this optional?
    BBlockContainerT& getPostDomFrontiers() { return postDomFrontiers; }
    const BBlockContainerT& getPostDomFrontiers() const { return postDomFrontiers; }

    bool addPostDomFrontier(BBlock<NodeT> *BB)
    {
        return postDomFrontiers.insert(BB);
    }

    void setIPostDom(BBlock<NodeT> *BB)
    {
        assert(!ipostdom && "Already has the immedate post-dominator");
        ipostdom = BB;
        BB->postDominators.insert(this);
    }

    BBlock<NodeT> *getIPostDom() { return ipostdom; }
    const BBlock<NodeT> *getIPostDom() const { return ipostdom; }
    BBlockContainerT& getPostDominators() { return postDominators; }
    const BBlockContainerT& getPostDominators() const { return postDominators; }

    unsigned int getDFSOrder() const
    {
        return analysisAuxData.dfsorder;
    }

    // in order to fasten up interprocedural analyses,
    // we register all the call sites in the BBlock
    unsigned int getCallSitesNum() const
    {
        return callSites.size();
    }

    const std::set<NodeT *>& getCallSites()
    {
        return callSites;
    }

    bool addCallsite(NodeT *n)
    {
        assert(n->getBBlock() == this
               && "Cannot add callsite from different BB");

        return callSites.insert(n).second;
    }

    bool removeCallSite(NodeT *n)
    {
        assert(n->getBBlock() == this
               && "Removing callsite from different BB");

        return callSites.erase(n) != 0;
    }

    void setSlice(uint64_t sid)
    {
        slice_id = sid;
    }

    uint64_t getSlice() const { return slice_id; }

private:
    // optional key
    KeyT key;

    // reference to dg if needed
    DependenceGraphT *dg;

    // nodes contained in this bblock
    std::list<NodeT *> nodes;

    SuccContainerT nextBBs;
    PredContainerT prevBBs;

    // when we have basic blocks, we do not need
    // to keep control dependencies in nodes, because
    // all nodes in block has the same control dependence
    BBlockContainerT controlDeps;
    BBlockContainerT revControlDeps;

    // post-dominator frontiers
    BBlockContainerT postDomFrontiers;
    BBlock<NodeT> *ipostdom;
    // the post-dominator tree edges
    // (reverse to immediate post-dominator)
    BBlockContainerT postDominators;

    // is this block in some slice?
    uint64_t slice_id;

    // auxiliary data for analyses
    std::set<NodeT *> callSites;

    // auxiliary data for different analyses
    analysis::AnalysesAuxiliaryData analysisAuxData;
    friend class analysis::BBlockAnalysis<NodeT>;
};

} // namespace dg

#endif // _BBLOCK_H_
