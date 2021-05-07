#ifndef DG_DFS_H_
#define DG_DFS_H_

#include "dg/ADT/Queue.h"
#include "dg/NodesWalk.h"

using dg::ADT::QueueLIFO;

namespace dg {

template <typename Node, typename VisitTracker = SetVisitTracker<Node>,
          typename EdgeChooser = SuccessorsEdgeChooser<Node>>
struct DFS
        : public NodesWalk<Node, QueueLIFO<Node *>, VisitTracker, EdgeChooser> {
    DFS() = default;
    DFS(EdgeChooser chooser)
            : NodesWalk<Node, QueueLIFO<Node *>, VisitTracker, EdgeChooser>(
                      std::move(chooser)) {}
    DFS(VisitTracker tracker)
            : NodesWalk<Node, QueueLIFO<Node *>, VisitTracker, EdgeChooser>(
                      std::move(tracker)) {}
    DFS(VisitTracker tracker, EdgeChooser chooser)
            : NodesWalk<Node, QueueLIFO<Node *>, VisitTracker, EdgeChooser>(
                      std::move(tracker), std::move(chooser)) {}
};

} // namespace dg

#endif
