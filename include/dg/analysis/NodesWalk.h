#ifndef _DG_NODES_WALK_H_
#define _DG_NODES_WALK_H_

#include <set>
#include <initializer_list>

namespace dg {
namespace analysis {

// universal but not very efficient visits tracker
template <typename Node>
struct SetVisitTracker {
    std::set<Node *> _visited;

    void visit(Node *n) { _visited.insert(n); }
    bool visited(Node *n) const { return _visited.count(n); }
};

// universal but not very efficient nodes info
template <typename Node>
struct SuccessorsEdgeChooser {
    class range {
        Node *_node;
    public:
        range(Node *n) : _node(n) {}

        auto begin() -> decltype(_node->getSuccessors().begin()) {
            return _node->getSuccessors().begin();
        }

        auto end() -> decltype(_node->getSuccessors().end()) {
            return _node->getSuccessors().end();
        }

        auto begin() const -> decltype(_node->getSuccessors().begin()) {
            return _node->getSuccessors().begin();
        }

        auto end() const -> decltype(_node->getSuccessors().end()) {
            return _node->getSuccessors().end();
        }
    };

    range operator()(Node *n) const { return range(n); }
};


template <typename Node, typename Queue,
          typename VisitTracker = SetVisitTracker<Node>,
          typename EdgeChooser = SuccessorsEdgeChooser<Node> >
class NodesWalk {
    EdgeChooser _chooser{};
    VisitTracker _visits{};
    Queue _queue{};

    void _enqueue(Node *n) {
        _queue.push(n);
        _visits.visit(n);
    }

    template <typename Func>
    void _run(Func F) {
        while (!_queue.empty()) {
            Node *current = _queue.pop();

            F(current);

            for (Node *succ : _chooser(current)) {
                if (!_visits.visited(succ)) {
                    _enqueue(succ);
                }
            }
        }
    }

public:
    NodesWalk() = default;

    NodesWalk(EdgeChooser&& chooser) : _chooser(std::move(chooser)) {}
    NodesWalk(VisitTracker&& tracker) : _visits(std::move(tracker)) {}
    NodesWalk(EdgeChooser&& chooser, VisitTracker tracker)
    : _chooser(std::move(chooser)), _visits(std::move(tracker)) {}

    template <typename Func>
    void run(Node *start, Func F) {
        _enqueue(start);
        _run(F);
    }

    template <typename Func, typename Container>
    void run(const Container& start, Func F) {
        for (Node *n : start)
            _enqueue(n);

        _run(F);
    }

    template <typename Func>
    void run(const std::initializer_list<Node*>& start, Func F) {
        for (Node *n : start)
            _enqueue(n);

        _run(F);
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_NODES_WALK_H_
