#ifndef _DG_DFS_H_
#define _DG_DFS_H_

#include "NodesWalk.h"
#include "ADT/Queue.h"

using dg::ADT::QueueLIFO;

namespace dg {
namespace analysis {

template <typename NodeT>
class DFS : public NodesWalk<NodeT, QueueLIFO<NodeT *>>
{
public:
    DFS<NodeT>() : dfsorder(0) {}

    template <typename FuncT, typename DataT>
    void run(NodeT *entry, FuncT func, DataT data,
             bool control = true, bool deps = true)
    {
        this->walk(entry, func, data, control, deps);
    }

    template <typename FuncT, typename DataT>
    void operator()(NodeT *entry, FuncT func, DataT data,
                    bool control = true, bool deps = true)
    {
        run(entry, func, data, control, deps);
    }

protected:
    /* virtual */
    void prepare(NodeT *BB)
    {
        // set dfs order number
        AnalysesAuxiliaryData& aad = this->getAnalysisData(BB);
        aad.dfsorder = ++dfsorder;
    }
private:
    unsigned int dfsorder;
};

#ifdef ENABLE_CFG

enum DFSFlags {
    DFS_INTERPROCEDURAL     = 1 << 0,
    DFS_PARAMS              = 1 << 1,
    DFS_BB_NO_CALLSITES     = 1 << 2,
};

static uint32_t
convFlags(uint32_t flags)
{
    uint32_t ret = 0;
    if (flags & DFS_INTERPROCEDURAL)
        ret |= BBLOCK_WALK_INTERPROCEDURAL;
    if (flags & DFS_PARAMS)
        ret |= BBLOCK_WALK_PARAMS;
    if (flags & DFS_BB_NO_CALLSITES)
        ret |= BBLOCK_NO_CALLSITES;

    return ret;
}

template <typename NodeT>
class BBlockDFS : public BBlockWalk<NodeT,
                                    QueueLIFO<BBlock<NodeT> *> >
{
public:
    typedef BBlock<NodeT> *BBlockPtrT;

    BBlockDFS<NodeT>(uint32_t fl = 0)
        : BBlockWalk<NodeT, QueueLIFO<BBlock<NodeT> *>>(convFlags(fl)),
          dfsorder(0), flags(fl) {}

    template <typename FuncT, typename DataT>
    void run(BBlockPtrT entry, FuncT func, DataT data)
    {
        this->walk(entry, func, data);
    }

    template <typename FuncT, typename DataT>
    void operator()(BBlockPtrT entry, FuncT func, DataT data)
    {
        run(entry, func, data);
    }

    uint32_t getFlags() const { return flags; }
protected:
    /* virtual */
    void prepare(BBlockPtrT BB)
    {
        // set dfs order number
        AnalysesAuxiliaryData& aad = this->getAnalysisData(BB);
        aad.dfsorder = ++dfsorder;
    }
private:
    unsigned int dfsorder;
    uint32_t flags;
};
#endif // ENABLE_CFG

} // namespace analysis
} // namespace dg

#endif // _DG_NODES_WALK_H_
