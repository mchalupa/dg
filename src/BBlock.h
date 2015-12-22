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
    BBlock<NodeT>(NodeT *head = nullptr)
        : key(KeyT()), ipostdom(nullptr), slice_id(0)
    {
        if (head)
            append(head);
    }

    typedef EdgesContainer<BBlock<NodeT>> ContainerT;

    const ContainerT& successors() const { return nextBBs; }
    const ContainerT& predecessors() const { return prevBBs; }
    const ContainerT& controlDependence() const { return controlDeps; }
    const ContainerT& RevControlDependence() const { return revControlDeps; }

    // similary to nodes, basic blocks can have keys
    // they are not stored anywhere, it is more due to debugging
    void setKey(const KeyT& k) { key = k; }
    const KeyT& getKey() const { return key; }

    ContainerT& getPostDomFrontiers() { return postDomFrontiers; }
    const ContainerT& getPostDomFrontiers() const { return postDomFrontiers; }

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
    ContainerT& getPostDominators() { return postDominators; }
    const ContainerT& getPostDominators() const { return postDominators; }

    typename ContainerT::size_type successorsNum() const
    {
        return nextBBs.size();
    }

    typename ContainerT::size_type predecessorsNum() const
    {
        return prevBBs.size();
    }

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
        for (auto pred : prevBBs) {
            pred->nextBBs.erase(this);
        }

        for (auto succ : nextBBs) {
            // redirect edges from each predecessor
            // to each successor
            for (auto pred : prevBBs) {
                pred->addSuccessor(succ);
            }

            succ->prevBBs.erase(this);
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

    bool addSuccessor(BBlock<NodeT> *b)
    {
        bool ret, ret2;

        // do not allow self-loops
        if (b == this)
            return false;

        ret = nextBBs.insert(b);
        ret2 = b->prevBBs.insert(this);

        // we either have both edges or none
        assert(ret == ret2);

        return ret;
    }

    bool addPredecessor(BBlock<NodeT> *b)
    {
        bool ret, ret2;

        // do not allow self-loops
        if (b == this)
            return false;

        ret = prevBBs.insert(b);
        ret2 = b->nextBBs.insert(this);

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
    size_t removePredecessor(BBlock<NodeT> *p)
    {
        size_t ret = 0;
        ret += p->nextBBs.erase(this);
        ret += prevBBs.erase(p);

        // return value 1 means bug
        assert(ret != 1 && "Bug in edges between basic blocks");

        return ret;
    }

    // return value is the same as with removePredecessor
    size_t removeSuccessor(BBlock<NodeT> *p)
    {
        size_t ret = 0;
        ret += p->prevBBs.erase(this);
        ret += nextBBs.erase(p);

        // return value 1 means bug
        assert(ret != 1 && "Bug in edges between basic blocks");

        return ret;
    }

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

    // nodes contained in this bblock
    std::list<NodeT *> nodes;

    ContainerT nextBBs;
    ContainerT prevBBs;

    // when we have basic blocks, we do not need
    // to keep control dependencies in nodes, because
    // all nodes in block has the same control dependence
    ContainerT controlDeps;
    ContainerT revControlDeps;

    // post-dominator frontiers
    ContainerT postDomFrontiers;
    BBlock<NodeT> *ipostdom;
    // the post-dominator tree edges
    // (reverse to immediate post-dominator)
    ContainerT postDominators;

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
