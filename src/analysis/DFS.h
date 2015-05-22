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

} // namespace analysis
} // namespace dg

#endif // _DG_NODES_WALK_H_
