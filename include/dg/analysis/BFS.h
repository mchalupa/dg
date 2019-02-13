#ifndef _DG_BFS_H_
#define _DG_BFS_H_

#include "dg/analysis/NodesWalk.h"
#include "dg/ADT/Queue.h"

using dg::ADT::QueueFIFO;

namespace dg {
namespace analysis {

template <typename Node,
          typename VisitTracker = SetVisitTracker<Node>,
          typename EdgeChooser = SuccessorsEdgeChooser<Node> >
struct BFS : public NodesWalk<Node, QueueFIFO<Node *>, VisitTracker, EdgeChooser> {
    BFS() = default;
    BFS(EdgeChooser chooser) : NodesWalk<Node, QueueFIFO<Node *>, VisitTracker, EdgeChooser>(std::move(chooser)) {}
    BFS(VisitTracker tracker) : NodesWalk<Node, QueueFIFO<Node *>, VisitTracker, EdgeChooser>(std::move(tracker)) {}
    BFS(EdgeChooser chooser, VisitTracker tracker)
    : NodesWalk<Node, QueueFIFO<Node *>, VisitTracker, EdgeChooser>(std::move(chooser), std::move(tracker)) {}
};

} // namespace analysis
} // namespace dg

#endif // _DG_BFS_H_
