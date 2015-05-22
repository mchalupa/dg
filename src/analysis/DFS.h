#ifndef _DG_DFS_H_
#define _DG_DFS_H_

#include "NodesWalk.h"
#include <queue>

namespace dg {
namespace analysis {

template <typename NodePtrT>
class DFS : public NodesWalk<NodePtrT, std::queue<NodePtrT> >
{
public:
    template <typename FuncT, typename DataT>
    void run(NodePtrT entry, FuncT func, DataT data,
             bool control = true, bool deps = true)
    {
        this->walk(entry, func, data, control, deps);
    }
};

#ifdef ENABLE_CFG
template <typename NodePtrT>
class BBlockDFS : public BBlockWalk<NodePtrT,
                                    std::queue<BBlock<NodePtrT> *> >
{
public:
    typedef BBlock<NodePtrT> *BBlockPtrT;

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
};
#endif // ENABLE_CFG

} // namespace analysis
} // namespace dg

#endif // _DG_NODES_WALK_H_
