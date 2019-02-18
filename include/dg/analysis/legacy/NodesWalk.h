#ifndef _DG_LEGACY_NODES_WALK_H_
#define _DG_LEGACY_NODES_WALK_H_

#include "dg/DGParameters.h"
#include "dg/analysis/legacy/Analysis.h"

namespace dg {
namespace analysis {
namespace legacy {

enum NodesWalkFlags {
    // do not walk any edges, user will
    // use enqueue method to decide which nodes
    // will be processed
    NODES_WALK_NONE_EDGES               = 0,
    NODES_WALK_INTERPROCEDURAL          = 1 << 0,
    NODES_WALK_CD                       = 1 << 1,
    NODES_WALK_DD                       = 1 << 2,
    NODES_WALK_REV_CD                   = 1 << 3,
    NODES_WALK_REV_DD                   = 1 << 4,
    NODES_WALK_USE                      = 1 << 5,
    NODES_WALK_USER                     = 1 << 6,
    NODES_WALK_ID                       = 1 << 7,
    NODES_WALK_REV_ID                   = 1 << 8,
    // Add to queue all first nodes of
    // node's BB successors
    NODES_WALK_BB_CFG                   = 1 << 9,
    // Add to queue all last nodes of
    // node's BB predecessors
    NODES_WALK_BB_REV_CFG               = 1 << 10,
    NODES_WALK_BB_POSTDOM               = 1 << 11,
    NODES_WALK_BB_POSTDOM_FRONTIERS     = 1 << 12,
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
    NodesWalk<NodeT, QueueT>(uint32_t opts = 0)
        : options(opts) {}

    template <typename FuncT, typename DataT>
    void walk(NodeT *entry, FuncT func, DataT data) {
        walk<FuncT, DataT>(std::set<NodeT *>{entry}, func, data);
    }

    template <typename FuncT, typename DataT>
    void walk(const std::set<NodeT *>& entry, FuncT func, DataT data)
    {
        run_id = ++NodesWalk<NodeT, QueueT>::walk_run_counter;

        assert(!entry.empty() && "Need entry node for traversing nodes");
        for (auto ent : entry)
            enqueue(ent);

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

            if (options & NODES_WALK_REV_CD) {
                processEdges(n->rev_control_begin(), n->rev_control_end());

#ifdef ENABLE_CFG
                // we can have control dependencies in BBlocks
                processBBlockRevCDs(n);
#endif // ENABLE_CFG
            }

            if (options & NODES_WALK_DD)
                processEdges(n->data_begin(), n->data_end());

            if (options & NODES_WALK_REV_DD)
                processEdges(n->rev_data_begin(), n->rev_data_end());

            if (options & NODES_WALK_USE)
                processEdges(n->use_begin(), n->use_end());

            if (options & NODES_WALK_USER)
                processEdges(n->user_begin(), n->user_end());

            if (options & NODES_WALK_ID)
                processEdges(n->interference_begin(), n->interference_end());

            if (options & NODES_WALK_REV_ID)
                processEdges(n->rev_interference_begin(), n->rev_interference_end());

#ifdef ENABLE_CFG
            if (options & NODES_WALK_BB_CFG)
                processBBlockCFG(n);

            if (options & NODES_WALK_BB_REV_CFG)
                processBBlockRevCFG(n);
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
        BBlock<NodeT> *BB = n->getBBlock();
        if (!BB)
            return;

        for (BBlock<NodeT> *CD : BB->revControlDependence())
            enqueue(CD->getLastNode());
    }

    void processBBlockCDs(NodeT *n)
    {
        BBlock<NodeT> *BB = n->getBBlock();
        if (!BB)
            return;

        for (BBlock<NodeT> *CD : BB->controlDependence())
            enqueue(CD->getFirstNode());
    }


    void processBBlockCFG(NodeT *n)
    {
        BBlock<NodeT> *BB = n->getBBlock();
        if (!BB)
            return;

        for (auto& E : BB->successors())
            enqueue(E.target->getFirstNode());
    }

    void processBBlockRevCFG(NodeT *n)
    {
        BBlock<NodeT> *BB = n->getBBlock();
        if (!BB)
            return;

        for (BBlock<NodeT> *S : BB->predecessors())
            enqueue(S->getLastNode());
    }

    void processBBlockPostDomFrontieres(NodeT *n)
    {
        BBlock<NodeT> *BB = n->getBBlock();
        if (!BB)
            return;

        for (BBlock<NodeT> *S : BB->getPostDomFrontiers())
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
    // walk dominator tree edges
    BBLOCK_WALK_DOM                 = 1 << 5,
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
    using BBlockPtrT = dg::BBlock<NodeT> *;

    BBlockWalk<NodeT, QueueT>(uint32_t fl = BBLOCK_WALK_CFG)
        : flags(fl) {}

    template <typename FuncT, typename DataT>
    void walk(BBlockPtrT entry, FuncT func, DataT data)
    {
        queue.push(entry);

        // set up the value of new walk and set it to entry node
        runid = ++BBlockWalk<NodeT, QueueT>::walk_run_counter;
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
                    // get callsites if bblocks does not keep them
                    for (NodeT *n : BB->getNodes()) {
                        if (n->hasSubgraphs())
                                BB->addCallsite(n);
                    }
                }

                if (BB->getCallSitesNum() != 0)
                    queueSubgraphsBBs(BB);
            }

            // queue post-dominated blocks if we should
            if (flags & BBLOCK_WALK_POSTDOM)
                for (BBlockPtrT S : BB->getPostDominators())
                    enqueue(S);

            // queue dominated blocks
            if (flags & BBLOCK_WALK_DOM)
                for (BBlockPtrT S : BB->getDominators())
                    enqueue(S);

            // queue sucessors of this BB
            if (flags & BBLOCK_WALK_CFG)
                for (auto& E : BB->successors())
                    enqueue(E.target);
        }
    }

    uint32_t getFlags() const { return flags; }

    void enqueue(BBlockPtrT BB)
    {
        AnalysesAuxiliaryData& sad = this->getAnalysisData(BB);
        if (sad.lastwalkid != runid) {
            sad.lastwalkid = runid;
            queue.push(BB);
        }
    }

protected:
    virtual void prepare(BBlockPtrT BB)
    {
        (void) BB;
    }

private:
    void queueSubgraphsBBs(BBlockPtrT BB)
    {
        DGParameters<NodeT> *params;

        // iterate over call-site nodes
        for (NodeT *cs : BB->getCallSites()) {
            // go through parameters if desired
            if ((flags & BBLOCK_WALK_PARAMS)) {
                params = cs->getParameters();
                if (params) {
                    enqueue(params->getBBIn());
                    enqueue(params->getBBOut());
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
                        enqueue(params->getBBIn());
                        enqueue(params->getBBOut());
                    }
                }

                // queue entry BBlock
                BBlockPtrT entryBB = subdg->getEntryBB();
                assert(entryBB && "No entry block in sub dg");
                enqueue(entryBB);
            }
        }
    }

    QueueT queue;
    uint32_t flags;
    unsigned int runid;
};

#endif

} // legacy
} // namespace analysis
} // namespace dg

#endif // _DG_NODES_WALK_H_
