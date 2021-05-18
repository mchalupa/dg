#ifndef DG_NODES_WALK_H_
#define DG_NODES_WALK_H_

#include <initializer_list>
#include <set>

namespace dg {

// universal but not very efficient visits tracker
template <typename Node>
struct SetVisitTracker {
    std::set<Node *> _visited{};

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

        auto begin() -> decltype(_node->successors().begin()) {
            return _node->successors().begin();
        }

        auto end() -> decltype(_node->successors().end()) {
            return _node->successors().end();
        }

        auto begin() const -> decltype(_node->successors().begin()) {
            return _node->successors().begin();
        }

        auto end() const -> decltype(_node->successors().end()) {
            return _node->successors().end();
        }
    };

    range operator()(Node *n) const { return range(n); }
};

namespace sfinae {
// std::void_t is from C++17...
template <typename... Ts>
struct make_void {
    using type = void;
};
template <typename... Ts>
using void_t = typename make_void<Ts...>::type;
} // namespace sfinae

// SFINAE check
template <typename T, typename = void>
struct has_foreach : std::false_type {};
template <typename T>
struct has_foreach<T, sfinae::void_t<decltype(&T::foreach)>> : std::true_type {
};

template <typename Node, typename Queue,
          typename VisitTracker = SetVisitTracker<Node>,
          typename EdgeChooser = SuccessorsEdgeChooser<Node>>
class NodesWalk {
    EdgeChooser _chooser{};
    VisitTracker _visits{};
    Queue _queue{};

    void _enqueue(Node *n) {
        _queue.push(n);
        _visits.visit(n);
    }

    // edge chooser uses operator()
    template <typename Func,
              typename std::enable_if<!has_foreach<EdgeChooser>::value,
                                      Func>::type * = nullptr>
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

    // edge chooser yields nodes using foreach()
    template <typename Func,
              typename std::enable_if<has_foreach<EdgeChooser>::value,
                                      Func>::type * = nullptr>
    void _run(Func F) {
        while (!_queue.empty()) {
            Node *current = _queue.pop();

            F(current);

            _chooser.foreach (current, [&](Node *n) {
                if (!_visits.visited(n)) {
                    _enqueue(n);
                }
            });
        }
    }

  public:
    NodesWalk() = default;

    NodesWalk(EdgeChooser &&chooser) : _chooser(std::move(chooser)) {}
    NodesWalk(VisitTracker &&tracker) : _visits(std::move(tracker)) {}
    NodesWalk(VisitTracker &&tracker, EdgeChooser &&chooser)
            : _chooser(std::move(chooser)), _visits(std::move(tracker)) {}

    template <typename Func>
    void run(Node *start, Func F) {
        _enqueue(start);
        _run(F);
    }

    template <typename Func, typename Container>
    void run(const Container &start, Func F) {
        for (Node *n : start)
            _enqueue(n);

        _run(F);
    }

    template <typename Func>
    void run(const std::initializer_list<Node *> &start, Func F) {
        for (Node *n : start)
            _enqueue(n);

        _run(F);
    }
};

} // namespace dg

#endif
