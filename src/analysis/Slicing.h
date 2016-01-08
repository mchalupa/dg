#ifndef _DG_SLICING_H_
#define _DG_SLICING_H_

#include "NodesWalk.h"
#include "BFS.h"
#include "ADT/Queue.h"
#include "DependenceGraph.h"

#ifdef ENABLE_CFG
#include "BBlock.h"
#endif

using dg::ADT::QueueFIFO;

namespace dg {
namespace analysis {

// this class will go through the nodes
// and will mark the ones that should be in the slice
template <typename NodeT>
class WalkAndMark : public NodesWalk<NodeT, QueueFIFO<NodeT *>>
{
public:
    WalkAndMark()
        : NodesWalk<NodeT, QueueFIFO<NodeT *>>(NODES_WALK_REV_CD |
                                               NODES_WALK_REV_DD |
                                               NODES_WALK_BB_POSTDOM_FRONTIERS) {}

    void mark(NodeT *start, uint32_t slice_id)
    {
        WalkData data(slice_id, this);
        this->walk(start, markSlice, &data);
    }

private:
    struct WalkData
    {
        WalkData(uint32_t si, WalkAndMark *wm)
            : slice_id(si), analysis(wm) {}

        uint32_t slice_id;
        WalkAndMark *analysis;
    };

    static void markSlice(NodeT *n, WalkData *data)
    {
        uint32_t slice_id = data->slice_id;
        n->setSlice(slice_id);

#ifdef ENABLE_CFG
        // when we marked a node, we need to mark even
        // the basic block - if there are basic blocks
        BBlock<NodeT> *B = n->getBBlock();
        if (B)
            B->setSlice(slice_id);
#endif

        // the same with dependence graph, if we keep a node from
        // a dependence graph, we need to keep the dependence graph
        DependenceGraph<NodeT> *dg = n->getDG();
        if (dg) {
            dg->setSlice(slice_id);
            // and keep also all call-sites of this func (they are
            // control dependent on the entry node)
            // This is correct but not so precise - fix it later.
            // Now I need the correctness...
            NodeT *entry = dg->getEntry();
            assert(entry && "No entry node in dg");
            data->analysis->enqueue(entry);
        }
    }
};

template <typename NodeT>
class Slicer : Analysis<NodeT>
{
    uint32_t options;
    uint32_t slice_id;

    void sliceGraph(DependenceGraph<NodeT> *dg, uint32_t slice_id)
    {
        for (auto it : *dg) {
            NodeT *n = it.second;

            // slice subgraphs if this node is a call-site
            for (DependenceGraph<NodeT> *sub : n->getSubgraphs())
                sliceGraph(sub, slice_id);

            if (n->getSlice() != slice_id) {
                // do graph specific logic
                removeNode(n);

                dg->deleteNode(n);
            }
        }

        // FIXME if graph own global nodes, slice the global nodes
    }

public:
    Slicer<NodeT>(uint32_t opt = 0)
        :options(opt), slice_id(0) {}

    uint32_t mark(NodeT *start, uint32_t sl_id = 0)
    {
        if (sl_id == 0)
            sl_id = ++slice_id;

        WalkAndMark<NodeT> wm;
        wm.mark(start, sl_id);

        return sl_id;
    }

    uint32_t slice(NodeT *start, uint32_t sl_id = 0)
    {
        // for now it will does the same as mark,
        // just remove the rest of nodes
        sl_id = mark(start, sl_id);
        sliceGraph(start->getDG(), sl_id);

        return sl_id;
    }

    // This method is called on nodes that are being
    // removed. Slicer implementation can override this
    // and use it as an event that particular node is
    // being removed and take action it needs
    virtual void removeNode(NodeT *node)
    {
        (void) node;
    }

#ifdef ENABLE_CFG
    virtual void removeBlock(BBlock<NodeT> *block)
    {
        (void) block;
    }

    struct RemoveBlockData {
        uint32_t sl_id;
        std::set<BBlock<NodeT> *>& blocks;
    };

    static void getBlocksToRemove(BBlock<NodeT> *BB, RemoveBlockData& data)
    {
        if (BB->getSlice() == data.sl_id)
            return;

        data.blocks.insert(BB);
    }

    // remove BBlocks that contain no node that should be in
    // sliced graph
    void sliceBBlocks(BBlock<NodeT> *start, uint32_t sl_id)
    {
        // we must queue the blocks ourselves before we potentially remove them
        BBlockBFS<NodeT> bfs(BFS_BB_CFG);
        std::set<BBlock<NodeT> *> blocks;

        RemoveBlockData data = { sl_id, blocks };
        bfs.run(start, getBlocksToRemove, data);

        for (BBlock<NodeT> *blk : blocks) {
            // call specific handlers
            removeBlock(blk);
            blk->remove();
        }
    }
#endif
};

} // namespace analysis
} // namespace dg

#endif // _DG_SLICING_H_
