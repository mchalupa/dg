#ifndef _DG_NODES_WALK_H_
#define _DG_NODES_WALK_H_

#include "Analysis.h"
#include "DGParameters.h"

namespace dg {
namespace analysis {

static unsigned int walk_run_counter;

enum NodesWalkFlags {
    NODES_WALK_INTERPROCEDURAL  = 1 << 0,
    NODES_WALK_CFG              = 1 << 1,
    NODES_WALK_CD               = 1 << 2,
    NODES_WALK_DD               = 1 << 3,
    NODES_WALK_REV_CD           = 1 << 4,
    NODES_WALK_REV_DD           = 1 << 5,
};

template <typename NodeT, typename QueueT>
class NodesWalk : public Analysis<NodeT>
{
public:
    NodesWalk<NodeT, QueueT>(uint32_t opts = NODES_WALK_CFG)
        : options(opts) {}

    template <typename FuncT, typename DataT>
    void walk(NodeT *entry, FuncT func, DataT data)
    {
        unsigned int run_id = ++walk_run_counter;
        QueueT queue;

        assert(entry && "Need entry node for traversing nodes");

        AnalysesAuxiliaryData& aad = this->getAnalysisData(entry);
        aad.lastwalkid = run_id;

        queue.push(entry);

        while (!queue.empty()) {
            NodeT *n = queue.pop();

            prepare(n);
            func(n, data);

            // add unprocessed vertices
            if (options & NODES_WALK_CD)
                processEdges(n->control_begin(),
                             n->control_end(), queue, run_id);

            if (options & NODES_WALK_DD)
                processEdges(n->data_begin(),
                             n->data_end(), queue, run_id);

            if (options & NODES_WALK_REV_CD)
                processEdges(n->rev_control_begin(),
                             n->rev_control_end(), queue, run_id);

            if (options & NODES_WALK_REV_DD)
                processEdges(n->rev_data_begin(),
                             n->rev_data_end(), queue, run_id);

            // FIXME add CFG and interprocedural
        }
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
    void processEdges(IT begin, IT end, QueueT& queue,
                      unsigned int run_id)
    {
        for (IT I = begin; I != end; ++I) {
            NodeT *tmp = *I;
            AnalysesAuxiliaryData& aad = this->getAnalysisData(tmp);

            if (aad.lastwalkid == run_id)
                continue;

            // mark node as visited
            aad.lastwalkid = run_id;
            queue.push(tmp);
        }
    }

    QueueT queue;
    uint32_t options;
};

enum BBlockWalkFlags {
    // recurse into procedures
    BBLOCK_WALK_INTERPROCEDURAL     = 1 << 0,
    // walk even through params
    BBLOCK_WALK_PARAMS              = 1 << 1,
    // need to go through the nodes once
    // because bblocks does not keep information
    // about call-sites
    BBLOCK_NO_CALLSITES             = 1 << 2,
};

#ifdef ENABLE_CFG
template <typename NodeT, typename QueueT>
class BBlockWalk : public BBlockAnalysis<NodeT>
{
public:
    typedef dg::BBlock<NodeT> *BBlockPtrT;

    BBlockWalk<NodeT, QueueT>(uint32_t fl = 0)
        : flags(fl) {}

    template <typename FuncT, typename DataT>
    void walk(BBlockPtrT entry, FuncT func, DataT data)
    {
        QueueT queue;
        queue.push(entry);

        // set up the value of new walk and set it to entry node
        unsigned int runid = ++walk_run_counter;
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

            // queue sucessors of this BB
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
