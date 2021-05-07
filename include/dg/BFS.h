#ifndef DG_BFS_H_
#define DG_BFS_H_

#include "dg/ADT/Queue.h"
#include "dg/NodesWalk.h"

using dg::ADT::QueueFIFO;

namespace dg {

template <typename Node, typename VisitTracker = SetVisitTracker<Node>,
          typename EdgeChooser = SuccessorsEdgeChooser<Node>>
struct BFS
        : public NodesWalk<Node, QueueFIFO<Node *>, VisitTracker, EdgeChooser> {
    BFS() = default;
    BFS(EdgeChooser chooser)
            : NodesWalk<Node, QueueFIFO<Node *>, VisitTracker, EdgeChooser>(
                      std::move(chooser)) {}
    BFS(VisitTracker tracker)
            : NodesWalk<Node, QueueFIFO<Node *>, VisitTracker, EdgeChooser>(
                      std::move(tracker)) {}
    BFS(VisitTracker tracker, EdgeChooser chooser)
            : NodesWalk<Node, QueueFIFO<Node *>, VisitTracker, EdgeChooser>(
                      std::move(tracker), std::move(chooser)) {}
};

} // namespace dg

#endif
