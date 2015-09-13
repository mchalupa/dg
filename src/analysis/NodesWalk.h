#ifndef _DG_NODES_WALK_H_
#define _DG_NODES_WALK_H_

#include "Analysis.h"
#include "DGParameters.h"

namespace dg {
namespace analysis {

enum NodesWalkFlags {
    // do not walk any edges, user will
    // use enqueue method to decide which nodes
    // will be processed
    NODES_WALK_NONE_EDGES               = 0,
    NODES_WALK_INTERPROCEDURAL          = 1 << 0,
    NODES_WALK_CFG                      = 1 << 1,
    NODES_WALK_REV_CFG                  = 1 << 2,
    NODES_WALK_CD                       = 1 << 3,
    NODES_WALK_DD                       = 1 << 4,
    NODES_WALK_REV_CD                   = 1 << 5,
    NODES_WALK_REV_DD                   = 1 << 6,
    // Add to queue all first nodes of
    // node's BB successors
    NODES_WALK_BB_CFG                   = 1 << 7,
    // Add to queue all last nodes of
    // node's BB predcessors
    NODES_WALK_BB_REV_CFG               = 1 << 8,
    NODES_WALK_BB_POSTDOM               = 1 << 9,
    NODES_WALK_BB_POSTDOM_FRONTIERS     = 1 << 10,
};

// this is a base class for nodes walk, it contains
// counter. If we would add counter (even static) into
// NodesWalk itself, we'd have counter for every
// NodeT+QueueT instantiation, which we don't want to,
// because then BFS and DFS would collide
template <typename NodeT>
class NodesWalkBase : public Analysis<NodeT>
{
protected:
    // this counter will increase each time we run
    // NodesWalk, so it can be used as an indicator
    // that we queued a node in a particular run or not
    static unsigned int walk_run_counter;
};

// counter definition
template<typename NodeT>
unsigned int NodesWalkBase<NodeT>::walk_run_counter = 0;

template <typename NodeT, typename QueueT>
class NodesWalk : public NodesWalkBase<NodeT>
{
public:
    NodesWalk<NodeT, QueueT>(uint32_t opts = NODES_WALK_CFG)
        : options(opts) {}

    template <typename FuncT, typename DataT>
    void walk(NodeT *entry, FuncT func, DataT data)
    {
        run_id = ++NodesWalk<NodeT, QueueT>::walk_run_counter;

        assert(entry && "Need entry node for traversing nodes");
        enqueue(entry);

        while (!queue.empty()) {
            NodeT *n = queue.pop();

            prepare(n);
            func(n, data);

            // do not try to process edges if we know
            // we should not
            if (options == 0)
                continue;

            // add unprocessed vertices
            if (options & NODES_WALK_CD) {
                processEdges(n->control_begin(), n->control_end());
#ifdef ENABLE_CFG
                // we can have control dependencies in BBlocks
                processBBlockCDs(n);
#endif // ENABLE_CFG
            }

            if (options & NODES_WALK_DD)
                processEdges(n->data_begin(), n->data_end());

            if (options & NODES_WALK_REV_CD) {
                processEdges(n->rev_control_begin(), n->rev_control_end());

#ifdef ENABLE_CFG
                // we can have control dependencies in BBlocks
                processBBlockRevCDs(n);
#endif // ENABLE_CFG
            }

            if (options & NODES_WALK_REV_DD)
                processEdges(n->rev_data_begin(), n->rev_data_end());

#ifdef ENABLE_CFG
            if (options & NODES_WALK_BB_CFG)
                processBBlockCFG(n);

            if (options & NODES_WALK_BB_REV_CFG)
                processBBlockRevCFG(n);

            if (options & NODES_WALK_CFG) {
                if (n->hasSuccessor())
                    enqueue(n->getSuccessor());
                else
                    processBBlockCFG(n);
            }

            if (options & NODES_WALK_REV_CFG) {
                if (n->hasPredcessor())
                    enqueue(n->getPredcessor());
                else
                    processBBlockRevCFG(n);
            }
#endif // ENABLE_CFG

            if (options & NODES_WALK_BB_POSTDOM_FRONTIERS)
                processBBlockPostDomFrontieres(n);

            // FIXME interprocedural
        }
    }

    // push a node into queue
    // This method is public so that analysis can
    // push some extra nodes into queue as they want.
    // They can also say that they don't want to process
    // any edges and take care of pushing the right nodes
    // on their own
    void enqueue(NodeT *n)
    {
            AnalysesAuxiliaryData& aad = this->getAnalysisData(n);

            if (aad.lastwalkid == run_id)
                return;

            // mark node as visited
            aad.lastwalkid = run_id;
            queue.push(n);
    }

protected:
    // function that will be called for all the nodes,
    // but is defined by the analysis framework, not
    // by the analysis itself. For example it may
    // assign DFS order numbers
    virtual void prepare(NodeT *n)
    {
        (void) n;
    }

private:
       template <typename IT>
    void processEdges(IT begin, IT end)
    {
        for (IT I = begin; I != end; ++I) {
            enqueue(*I);
        }
    }

#ifdef ENABLE_CFG
    // we can have control dependencies in BBlocks
    void processBBlockRevCDs(NodeT *n)
    {
        // push terminator nodes of all blocks that are
        // control dependent
        BBlock<NodeT> *BB = n->getBasicBlock();
        if (!BB)
            return;

        for (BBlock<NodeT> *CD : BB->RevControlDependence())
            enqueue(CD->getLastNode());
    }

    void processBBlockCDs(NodeT *n)
    {
        BBlock<NodeT> *BB = n->getBasicBlock();
        if (!BB)
            return;

        for (BBlock<NodeT> *CD : BB->controlDependence())
            enqueue(CD->getFirstNode());
    }

    void processBBlockCFG(NodeT *n)
    {
        BBlock<NodeT> *BB = n->getBasicBlock();
        if (!BB)
            return;

        for (BBlock<NodeT> *S : BB->successors())
            enqueue(S->getFirstNode());
    }

    void processBBlockRevCFG(NodeT *n)
    {
        BBlock<NodeT> *BB = n->getBasicBlock();
        if (!BB)
            return;

        for (BBlock<NodeT> *S : BB->predcessors())
            enqueue(S->getLastNode());
    }

    void processBBlockPostDomFrontieres(NodeT *n)
    {
        BBlock<NodeT> *BB = n->getBasicBlock();
        if (!BB)
            return;

        for (BBlock<NodeT> *S : BB->getPostDomFrontiers())
            if (S != BB)
                enqueue(S->getLastNode());
    }
#endif // ENABLE_CFG

    QueueT queue;
    // id of particular nodes walk
    unsigned int run_id;
    uint32_t options;
};

enum BBlockWalkFlags {
    // recurse into procedures
    BBLOCK_WALK_INTERPROCEDURAL     = 1 << 0,
    // walk even through params
    BBLOCK_WALK_PARAMS              = 1 << 1,
    // walk post-dominator tree edges
    BBLOCK_WALK_POSTDOM             = 1 << 2,
    // walk normal CFG edges
    BBLOCK_WALK_CFG                 = 1 << 3,
    // need to go through the nodes once
    // because bblocks does not keep information
    // about call-sites
    BBLOCK_NO_CALLSITES             = 1 << 4,
};

// this is a base class for bblock walk, it contains
// counter. If we would add counter (even static) into
// NodesWalk itself, we'd have counter for every
// NodeT+QueueT instantiation, which we don't want to,
// because then BFS and DFS would collide
template <typename NodeT>
class BBlockWalkBase : public BBlockAnalysis<NodeT>
{
protected:
    // this counter will increase each time we run
    // NodesWalk, so it can be used as an indicator
    // that we queued a node in a particular run or not
    static unsigned int walk_run_counter;
};

// counter definition
template<typename NodeT>
unsigned int BBlockWalkBase<NodeT>::walk_run_counter = 0;

#ifdef ENABLE_CFG
template <typename NodeT, typename QueueT>
class BBlockWalk : public BBlockWalkBase<NodeT>
{
public:
    typedef dg::BBlock<NodeT> *BBlockPtrT;

    BBlockWalk<NodeT, QueueT>(uint32_t fl = BBLOCK_WALK_CFG)
        : flags(fl) {}

    template <typename FuncT, typename DataT>
    void walk(BBlockPtrT entry, FuncT func, DataT data)
    {
        QueueT queue;
        queue.push(entry);

        // set up the value of new walk and set it to entry node
        unsigned int runid = ++BBlockWalk<NodeT, QueueT>::walk_run_counter;
        AnalysesAuxiliaryData& aad = this->getAnalysisData(entry);
        aad.lastwalkid = runid;

        while (!queue.empty()) {
            BBlockPtrT BB = queue.pop();

            prepare(BB);
            func(BB, data);

            // update statistics
            ++this->statistics.processedBlocks;

            // should and can we go into subgraph?
            if ((flags & BBLOCK_WALK_INTERPROCEDURAL)) {
                if ((flags & BBLOCK_NO_CALLSITES)
                    && BB->getCallSitesNum() == 0) {
                    // get callsites if bblocks does not
                    // keep them
                    NodeT *n = BB->getFirstNode();
                    while (n) {
                        if (n->hasSubgraphs())
                                BB->addCallsite(n);

                        n = n->getSuccessor();
                    }
                }

                if (BB->getCallSitesNum() != 0)
                    queueSubgraphsBBs(BB, queue, runid);
            }

            // queue post-dominated blocks if we should
            if (flags & BBLOCK_WALK_POSTDOM)
                for (BBlockPtrT S : BB->getPostDominators())
                    queuePush(S, queue, runid);

            // queue sucessors of this BB
            if (flags & BBLOCK_WALK_CFG)
                for (BBlockPtrT S : BB->successors())
                    queuePush(S, queue, runid);
        }
    }

    uint32_t getFlags() const { return flags; }

protected:
    virtual void prepare(BBlockPtrT BB)
    {
        (void) BB;
    }

private:
    void queuePush(BBlockPtrT BB, QueueT& queue, unsigned int runid)
    {
        AnalysesAuxiliaryData& sad = this->getAnalysisData(BB);
        if (sad.lastwalkid != runid) {
            sad.lastwalkid = runid;
            queue.push(BB);
        }
    }

    void queueSubgraphsBBs(BBlockPtrT BB, QueueT& queue, unsigned int runid)
    {
        DGParameters<typename NodeT::KeyType, NodeT> *params;

        // iterate over call-site nodes
        for (NodeT *cs : BB->getCallSites()) {
            // go through parameters if desired
            if ((flags & BBLOCK_WALK_PARAMS)) {
                params = cs->getParameters();
                if (params) {
                    queuePush(params->getBBIn(), queue, runid);
                    queuePush(params->getBBOut(), queue, runid);
                }
            }

            // iterate over subgraphs in call-site node
            // and put into queue entry blocks
            for (auto subdg : cs->getSubgraphs()) {
                // go into formal parameters if wanted
                if ((flags & BBLOCK_WALK_PARAMS)) {
                    NodeT *entry = subdg->getEntry();
                    assert(entry && "No entry node in sub dg");

                    params = entry->getParameters();
                    if (params) {
                        queuePush(params->getBBIn(), queue, runid);
                        queuePush(params->getBBOut(), queue, runid);
                    }
                }

                // queue entry BBlock
                BBlockPtrT entryBB = subdg->getEntryBB();
                assert(entryBB && "No entry block in sub dg");
                queuePush(entryBB, queue, runid);
            }
        }
    }

    uint32_t flags;
};

#endif

} // namespace analysis
} // namespace dg

#endif // _DG_NODES_WALK_H_
