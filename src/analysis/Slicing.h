#ifndef _DG_SLICING_H_
#define _DG_SLICING_H_

#include "DFS.h"
#include "DependenceGraph.h"

#ifdef ENABLE_CFG
#include "BBlock.h"
#endif

namespace dg {
namespace analysis {

template <typename NodeT>
class Slicer : Analysis<NodeT>
{
    uint32_t options;
    uint32_t slice_id;

    static void markSlice(NodeT *n, uint32_t slice_id)
    {
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
        if (dg)
            dg->setSlice(slice_id);
    }

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

        DFS<NodeT> dfs(DFS_REV_CD | DFS_REV_DD | DFS_BB_POSTDOM_FRONTIERS);
        dfs.walk(start, markSlice, sl_id);

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

    virtual void removeNode(NodeT *node)
    {
        (void) node;
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_SLICING_H_
