/// XXX add licence
//

#ifndef _CFG_H_
#define _CFG_H_

#include <set>
#include <cassert>

#ifndef ENABLE_CFG
#error "CFG.h needs be included with ENABLE_CFG"
#endif // ENABLE_CFG

namespace dg {

/// ------------------------------------------------------------------
// - BBlock
//     Basic block structure for dependence graph
/// ------------------------------------------------------------------
template <typename NodePtrT>
class BBlock
{
public:
    BBlock<NodePtrT>(NodePtrT first, NodePtrT last = nullptr)
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
    typedef std::set<BBlock<NodePtrT> *> ContainerT;

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

    bool addSuccessor(BBlock<NodePtrT> *b)
    {
        bool ret, ret2;
        ret = nextBBs.insert(b).second;
        ret2 = b->prevBBs.insert(this).second;

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
    size_t removePredcessor(BBlock<NodePtrT> *p)
    {
        size_t ret = 0;
        ret += p->nextBBs.erase(this);
        ret += prevBBs.erase(p);

        // return value 1 means bug
        assert(ret != 1 && "Bug in edges between basic blocks");

        return ret;
    }

    // return value is the same as with removePredcessor
    size_t removeSuccessor(BBlock<NodePtrT> *p)
    {
        size_t ret = 0;
        ret += p->prevBBs.erase(this);
        ret += nextBBs.erase(p);

        // return value 1 means bug
        assert(ret != 1 && "Bug in edges between basic blocks");

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
    BBlock<NodePtrT> *getIPostDomBy() const { return ipostdom; }
    // add node that is immediately post-dominated by this node
    bool addIPostDom(BBlock<NodePtrT> *pd)
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
    BBlock<NodePtrT> *ipostdom;
#endif // ENABLE_POSTDOM

    // helper variable for running DFS/BFS on the BasicBlocks
    unsigned int dfs_run;
};

} // namespace dg

#endif // _CFG_H_
