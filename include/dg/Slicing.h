#ifndef DG_SLICING_H_
#define DG_SLICING_H_

#include <set>

#include "dg/ADT/Queue.h"
#include "dg/DependenceGraph.h"
#include "dg/legacy/Analysis.h"
#include "dg/legacy/BFS.h"
#include "dg/legacy/NodesWalk.h"

#ifdef ENABLE_CFG
#include "dg/BBlock.h"
#endif

namespace dg {

// this class will go through the nodes
// and will mark the ones that should be in the slice
template <typename NodeT>
class WalkAndMark
        : public legacy::NodesWalk<NodeT, dg::ADT::QueueFIFO<NodeT *>> {
    using Queue = dg::ADT::QueueFIFO<NodeT *>;

  public:
    ///
    // forward_slc makes searching the dependencies
    // in forward direction instead of backward
    WalkAndMark(bool forward_slc = false)
            : legacy::NodesWalk<NodeT, Queue>(
                      forward_slc
                              ? (legacy::NODES_WALK_DD | // legacy::NODES_WALK_CD
                                                         // NOTE: we handle CD
                                                         // separately
                                 legacy::NODES_WALK_USE | legacy::NODES_WALK_ID)
                              : (legacy::NODES_WALK_REV_CD |
                                 legacy::NODES_WALK_REV_DD |
                                 legacy::NODES_WALK_USER |
                                 legacy::NODES_WALK_ID |
                                 legacy::NODES_WALK_REV_ID)),
              forward_slice(forward_slc) {}

    void mark(const std::set<NodeT *> &start, uint32_t slice_id) {
        WalkData data(slice_id, this, forward_slice ? &markedBlocks : nullptr);
        this->walk(start, markSlice, &data);
    }

    void mark(NodeT *start, uint32_t slice_id) {
        WalkData data(slice_id, this, forward_slice ? &markedBlocks : nullptr);
        this->walk(start, markSlice, &data);
    }

    bool isForward() const { return forward_slice; }
    // returns marked blocks, but only for forward slicing atm
    const std::set<BBlock<NodeT> *> &getMarkedBlocks() { return markedBlocks; }

  private:
    bool forward_slice{false};
    std::set<BBlock<NodeT> *> markedBlocks;

    struct WalkData {
        WalkData(uint32_t si, WalkAndMark *wm,
                 std::set<BBlock<NodeT> *> *mb = nullptr)
                : slice_id(si), analysis(wm)
#ifdef ENABLE_CFG
                  ,
                  markedBlocks(mb)
#endif
        {
        }

        uint32_t slice_id;
        WalkAndMark *analysis;
#ifdef ENABLE_CFG
        std::set<BBlock<NodeT> *> *markedBlocks;
#endif
    };

    static void markSlice(NodeT *n, WalkData *data) {
        uint32_t slice_id = data->slice_id;
        n->setSlice(slice_id);

#ifdef ENABLE_CFG
        // when we marked a node, we need to mark even
        // the basic block - if there are basic blocks
        if (BBlock<NodeT> *B = n->getBBlock()) {
            B->setSlice(slice_id);
            if (data->markedBlocks) {
                data->markedBlocks->insert(B);
            }

            // if this node has CDs, enque them
            if (data->analysis->isForward()) {
                for (auto it = n->control_begin(), et = n->control_end();
                     it != et; ++it) {
                    data->analysis->enqueue(*it);
                }

                // if this node is a jump instruction,
                // add also nodes that control depend on this jump
                if (n == B->getLastNode()) {
                    for (BBlock<NodeT> *CD : B->controlDependence()) {
                        for (auto *cdnd : CD->getNodes()) {
                            data->analysis->enqueue(cdnd);
                        }
                    }
                }
            }
        }
#endif

        // the same with dependence graph, if we keep a node from
        // a dependence graph, we need to keep the dependence graph
        if (DependenceGraph<NodeT> *dg = n->getDG()) {
            dg->setSlice(slice_id);
            if (!data->analysis->isForward()) {
                // and keep also all call-sites of this func (they are
                // control dependent on the entry node)
                // This is correct but not so precise - fix it later.
                // Now I need the correctness...
                NodeT *entry = dg->getEntry();
                assert(entry && "No entry node in dg");
                data->analysis->enqueue(entry);
            }
        }
    }
};

struct SlicerStatistics {
    SlicerStatistics() = default;

    // total number of nodes that were checked for removing
    uint64_t nodesTotal{0};
    // total number of nodes actually removed (including the
    // ones removed in blocks)
    uint64_t nodesRemoved{0};
    // number of whole blocks removed
    uint32_t blocksRemoved{0};
};

template <typename NodeT>
class Slicer : legacy::Analysis<NodeT> {
    uint32_t options;
    uint32_t slice_id{0};

    std::set<DependenceGraph<NodeT> *> sliced_graphs;

    // slice nodes from the graph; do it recursively for call-nodes
    void sliceNodes(DependenceGraph<NodeT> *dg, uint32_t slice_id) {
        for (auto &it : *dg) {
            NodeT *n = it.second;

            if (n->getSlice() != slice_id) {
                if (removeNode(n)) // do backend's specific logic
                    dg->deleteNode(n);

                continue;
            }

            // slice subgraphs if this node is
            // a call-site that is in the slice
            for (DependenceGraph<NodeT> *sub : n->getSubgraphs()) {
                // slice the subgraph if we haven't sliced it yet
                if (sliced_graphs.insert(sub).second)
                    sliceNodes(sub, slice_id);
            }
        }

        // slice the global nodes
        const auto &global_nodes = dg->getGlobalNodes();
        if (!global_nodes)
            return;

        for (auto &it : *global_nodes.get()) {
            NodeT *n = it.second;

            if (n->getSlice() != slice_id) {
                if (removeNode(n)) // do backend's specific logic
                    dg->deleteGlobalNode(n);
                continue;
            }
        }
    }

  protected:
    // how many nodes and blocks were removed or kept
    SlicerStatistics statistics;

  public:
    Slicer<NodeT>(uint32_t opt = 0) : options(opt) {}

    SlicerStatistics &getStatistics() { return statistics; }
    const SlicerStatistics &getStatistics() const { return statistics; }

    ///
    // Mark nodes dependent on 'start' with 'sl_id'.
    // If 'forward_slice' is true, mark the nodes depending on 'start' instead.
    uint32_t mark(NodeT *start, uint32_t sl_id = 0,
                  bool forward_slice = false) {
        if (sl_id == 0)
            sl_id = ++slice_id;

        WalkAndMark<NodeT> wm(forward_slice);
        wm.mark(start, sl_id);

        ///
        // If we are performing forward slicing,
        // we must do the slice executable as we now just
        // marked the nodes that are data dependent on the
        // slicing criterion. We do that by using these
        // nodes as slicing criteria in normal backward slicing.
        if (forward_slice) {
            std::set<NodeT *> inslice;
            for (auto *BB : wm.getMarkedBlocks()) {
#if ENABLE_CFG
                for (auto *nd : BB->getNodes()) {
                    if (nd->getSlice() == sl_id) {
                        inslice.insert(nd);
                    }
                }
#endif
            }

            // do backward slicing to make the slice executable
            if (!inslice.empty()) {
                WalkAndMark<NodeT> wm2;
                wm2.mark(inslice, sl_id);
            }
        }

        return sl_id;
    }

    // slice the graph and its subgraphs. mark needs to be called
    // before this routine (otherwise everything is sliced)
    uint32_t slice(DependenceGraph<NodeT> *dg, uint32_t sl_id = 0) {
#ifdef ENABLE_CFG
        // first slice away bblocks that should go away
        sliceBBlocks(dg, sl_id);
#endif // ENABLE_CFG

        // now slice the nodes from the remaining graphs
        sliceNodes(dg, sl_id);

        return sl_id;
    }

    // remove node from the graph
    // This virtual method allows to taky an action
    // when node is being removed from the graph. It can also
    // disallow removing this node by returning false
    virtual bool removeNode(NodeT * /*unused*/) { return true; }

#ifdef ENABLE_CFG
    virtual bool removeBlock(BBlock<NodeT> * /*unused*/) { return true; }

    struct RemoveBlockData {
        uint32_t sl_id;
        std::set<BBlock<NodeT> *> &blocks;
    };

    static void getBlocksToRemove(BBlock<NodeT> *BB, RemoveBlockData &data) {
        if (BB->getSlice() == data.sl_id)
            return;

        data.blocks.insert(BB);
    }

    void sliceBBlocks(BBlock<NodeT> *start, uint32_t sl_id) {
        // we must queue the blocks ourselves before we potentially remove them
        legacy::BBlockBFS<NodeT> bfs(legacy::BFS_BB_CFG);
        std::set<BBlock<NodeT> *> blocks;

        RemoveBlockData data = {sl_id, blocks};
        bfs.run(start, getBlocksToRemove, data);

        for (BBlock<NodeT> *blk : blocks) {
            // update statistics
            statistics.nodesRemoved += blk->size();
            statistics.nodesTotal += blk->size();
            ++statistics.blocksRemoved;

            // call specific handlers (overriden by child class)
            removeBlock(blk);

            // remove block from the graph
            blk->remove();
        }
    }

    // remove BBlocks that contain no node that should be in
    // sliced graph
    void sliceBBlocks(DependenceGraph<NodeT> *graph, uint32_t sl_id) {
        auto &CB = graph->getBlocks();
#ifndef NDEBUG
        uint32_t blocksNum = CB.size();
#endif
        // gather the blocks
        // FIXME: we don't need two loops, just go carefully
        // through the constructed blocks (keep temporary always-valid iterator)
        std::set<BBlock<NodeT> *> blocks;
        for (auto &it : CB) {
            if (it.second->getSlice() != sl_id)
                blocks.insert(it.second);
        }

        for (BBlock<NodeT> *blk : blocks) {
            // update statistics
            statistics.nodesRemoved += blk->size();
            statistics.nodesTotal += blk->size();
            ++statistics.blocksRemoved;

            // call specific handlers (overriden by child class)
            if (removeBlock(blk)) {
                // remove block from the graph
                blk->remove();
            }
        }

        assert(CB.size() + blocks.size() == blocksNum &&
               "Inconsistency in sliced blocks");
    }

#endif
};

} // namespace dg

#endif
