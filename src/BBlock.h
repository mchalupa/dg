/// XXX add licence
//

#ifndef _BBLOCK_H_
#define _BBLOCK_H_

#include <cassert>

#include "EdgesContainer.h"
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
    BBlock<NodeT>(NodeT *first = nullptr, NodeT *last = nullptr)
        : firstNode(first), lastNode(last)
#if defined(ENABLE_POSTDOM)
          , ipostdom(nullptr)
#endif
    {
        /// XXX other nodes do not have set basicBlock
        // shouldn't we keep a list of the nodes the
        // basic block contains? In that case we would
        // keep only basic blocks in the dependence graph
        if (first)
            first->setBasicBlock(this);
        if (last)
            last->setBasicBlock(this);
    }

    typedef EdgesContainer<BBlock<NodeT>> ContainerT;

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

    // remove all edges from/to this BB
    void isolate()
    {
        for (auto succ : nextBBs) {
            // redirect edges from each predcessor
            // to each successor
            for (auto pred : prevBBs) {
                pred->addSuccessor(succ);
            }

            // remove edges from this node to the successor,
            // we have reconnect all the edges for this one
            removeSuccessor(succ);
        }

        // we still have edges to predcessors
        // XXX is it safe to modify the set
        // while iterating?
        for (auto pred : prevBBs)
            removePredcessor(pred);

    }

    void remove(bool with_nodes = true)
    {
        // do not leave any dangling reference
        isolate();

        // XXX what to do when this is entry block?

        if (with_nodes) {
            NodeT *n = firstNode;
            NodeT *tmp;
            while (n) {
                tmp = n;
                n = n->getSuccessor();

                // we must set basic block to nullptr
                // otherwise the node will try to remove the
                // basic block again if it is of size 1
                tmp->setBasicBlock(nullptr);

                // remove dependency edges, let be CFG edges
                // as we'll destroy all the nodes
                tmp->removeCDs();
                tmp->removeDDs();
                // remove the node from dg
                tmp->removeFromDG();

                delete tmp;
            }
        }

        delete this;
    }

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

    bool addPredcessor(BBlock<NodeT> *b)
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
    size_t removePredcessor(BBlock<NodeT> *p)
    {
        size_t ret = 0;
        ret += p->nextBBs.erase(this);
        ret += prevBBs.erase(p);

        // return value 1 means bug
        assert(ret != 1 && "Bug in edges between basic blocks");

        return ret;
    }

    // return value is the same as with removePredcessor
    size_t removeSuccessor(BBlock<NodeT> *p)
    {
        size_t ret = 0;
        ret += p->prevBBs.erase(this);
        ret += nextBBs.erase(p);

        // return value 1 means bug
        assert(ret != 1 && "Bug in edges between basic blocks");

        return ret;
    }

    NodeT *getFirstNode() const { return firstNode; }
    NodeT *getLastNode() const { return lastNode; }

    NodeT *setLastNode(NodeT *nn)
    {
        NodeT *old = lastNode;
        lastNode = nn;
        return old;
    }

    NodeT *setFirstNode(NodeT *nn)
    {
        NodeT *old = firstNode;
        firstNode = nn;
        return old;
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
        assert(n->getBasicBlock() == this
               && "Cannot add callsite from different BB");

        return callSites.insert(n).second;
    }

    bool removeCallSite(NodeT *n)
    {
        assert(n->getBasicBlock() == this
               && "Removing callsite from different BB");

        return callSites.erase(n) != 0;
    }

#if defined(ENABLE_POSTDOM)
    ContainerT& getPostdominates() { return postdominates; }
    // get immediate post-dominator
    const ContainerT& getIPostDom() const { return ipostdominates; }
    BBlock<NodeT> *getIPostDomBy() const { return ipostdom; }
    // add node that is immediately post-dominated by this node
    bool addIPostDom(BBlock<NodeT> *pd)
    {
        assert(pd && "Passed nullptr");

        if (pd == ipostdom)
            return false;

        /*
        assert((pd == nullptr || pd == ipostdom)
               && "Node already has different post-dominator");
        */

        ipostdom = pd;
        return pd->ipostdominates.insert(this);
    }
#endif // ENABLE_POSTDOM

private:
    ContainerT nextBBs;
    ContainerT prevBBs;

    // first node in this basic block
    NodeT *firstNode;
    // last node in this basic block
    NodeT *lastNode;

#if defined(ENABLE_POSTDOM)
    // set of blocks that this block post-dominates
    ContainerT postdominates;

    // immediate postdominator. The BB can be immediate
    // post-dominator of more nodes
    ContainerT ipostdominates;

    // reverse edge to immediate postdom. The BB can be
    // immediately post-dominated only by one BB
    BBlock<NodeT> *ipostdom;

#endif // ENABLE_POSTDOM

    // auxiliary data for analyses
    std::set<NodeT *> callSites;

    // auxiliary data for different analyses
    analysis::AnalysesAuxiliaryData analysisAuxData;
    friend class analysis::BBlockAnalysis<NodeT>;
};

} // namespace dg

#endif // _BBLOCK_H_
