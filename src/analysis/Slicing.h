#ifndef _DG_SLICING_H_
#define _DG_SLICING_H_

#include "DFS.h"
#include "DependenceGraph.h"

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
    }

    void sliceGraph(DependenceGraph<NodeT> *dg, uint32_t slice_id)
    {
        for (auto it : *dg) {
            NodeT *n = it.second;

            // slice subgraphs if this node is a call-site
            for (DependenceGraph<NodeT> *sub : n->getSubgraphs())
                sliceGraph(sub, slice_id);

            // do graph specific logic
            removeNode(n);

            if (n->getSlice() != slice_id)
                dg->deleteNode(n);
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

        DFS<NodeT> dfs(DFS_REV_CD | DFS_REV_DD | DFS_REV_CFG);
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
