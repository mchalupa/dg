#ifndef DG_POINTER_GRAPH_H_
#define DG_POINTER_GRAPH_H_

#include "dg/ADT/Queue.h"
#include "dg/BFS.h"
#include "dg/CallGraph/CallGraph.h"
#include "dg/PointerAnalysis/PSNode.h"
#include "dg/SCC.h"
#include "dg/SubgraphNode.h"
#include "dg/util/debug.h"

#include <cassert>
#include <cstdarg>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dg {
namespace pta {

// special nodes and pointers to them

class PointerGraph;

// A single procedure in Pointer Graph
class PointerSubgraph {
    friend class PointerGraph;

    unsigned _id{0};

    PointerSubgraph(unsigned id, PSNode *r1, PSNode *va = nullptr)
            : _id(id), root(r1), vararg(va) {}

    // non-trivial strongly connected components
    bool _computed_loops{false};
    std::vector<std::vector<PSNode *>> _loops;
    std::unordered_map<const PSNode *, size_t> _node_to_loop;

  public:
    PointerSubgraph(PointerSubgraph &&) = default;
    PointerSubgraph() = default;
    PointerSubgraph(const PointerSubgraph &) = delete;

    unsigned getID() const { return _id; }

    // FIXME: make the attrs private

    // the first node of the subgraph. XXX: rename to entry
    PSNode *root{nullptr};

    // return nodes of this graph
    std::set<PSNode *> returnNodes{};

    // this is the node where we gather the variadic-length arguments
    PSNode *vararg{nullptr};

    PSNode *getRoot() { return root; }
    const PSNode *getRoot() const { return root; }

    bool computedLoops() const { return _computed_loops; }

    // information about loops in this subgraph
    const std::vector<PSNode *> *getLoop(const PSNode *nd) const {
        assert(_computed_loops && "Call computeLoops() first");

        auto it = _node_to_loop.find(nd);
        assert(it == _node_to_loop.end() || it->second < _loops.size());
        return it == _node_to_loop.end() ? nullptr : &_loops[it->second];
    }

    const std::vector<std::vector<PSNode *>> &getLoops() const {
        assert(_computed_loops && "Call computeLoops() first");
        return _loops;
    }

    const std::vector<std::vector<PSNode *>> &getLoops() {
        if (!computedLoops())
            computeLoops();
        return _loops;
    }

    // FIXME: remember just that a node is on loop, not the whole loops
    void computeLoops();
};

// IDs of special nodes
enum PointerGraphReservedIDs {
    ID_UNKNOWN = 1,
    ID_NULL = 2,
    ID_INVALIDATED = 3,
    LAST_RESERVED_ID = 3
};

///
// Basic graph for pointer analysis
// -- contains CFG graphs for all procedures of the program.
class PointerGraph {
    unsigned int dfsnum{0};

    // root of the pointer state subgraph
    PointerSubgraph *_entry{nullptr};

    using NodesT = std::vector<std::unique_ptr<PSNode>>;
    using GlobalNodesT = std::vector<PSNode *>;
    using SubgraphsT = std::vector<std::unique_ptr<PointerSubgraph>>;

    NodesT nodes;
    SubgraphsT _subgraphs;

    // Take care of assigning ids to new nodes
    unsigned int last_node_id = PointerGraphReservedIDs::LAST_RESERVED_ID;
    unsigned int getNewNodeId() { return ++last_node_id; }

    GenericCallGraph<PSNode *> callGraph;
    GlobalNodesT _globals;

    // check for correct count of variadic arguments
    template <PSNodeType type, size_t actual_size>
    constexpr static ssize_t expected_args_size() {
        // C++14 TODO: replace this atrocity with a switch
        return type == PSNodeType::NOOP || type == PSNodeType::ENTRY ||
                               type == PSNodeType::FUNCTION ||
                               type == PSNodeType::FORK ||
                               type == PSNodeType::JOIN
                       ? 0
               : type == PSNodeType::CAST || type == PSNodeType::LOAD ||
                               type == PSNodeType::INVALIDATE_OBJECT ||
                               type == PSNodeType::INVALIDATE_LOCALS ||
                               type == PSNodeType::FREE
                       ? 1
               : type == PSNodeType::STORE || type == PSNodeType::CONSTANT ? 2
               : type == PSNodeType::CALL || type == PSNodeType::CALL_FUNCPTR ||
                               type == PSNodeType::CALL_RETURN ||
                               type == PSNodeType::PHI ||
                               type == PSNodeType::RETURN
                       ? actual_size
                       : -1;
    }

    // C++17 TODO:
    //   * replace the two definitions of nodeFactory with one using
    //     `if constexpr (needs_type)`
    //   * replace GetNodeType with a chain of `if constexpr` expressions
    template <PSNodeType Type, typename... Args,
              typename Node = typename GetNodeType<Type>::type>
    typename std::enable_if<!std::is_same<Node, PSNode>::value, Node *>::type
    nodeFactory(Args &&...args) {
        return new Node(getNewNodeId(), std::forward<Args>(args)...);
    }

    // we need to check that the number of arguments is correct with general
    // PSNode
    template <PSNodeType Type, typename... Args,
              typename Node = typename GetNodeType<Type>::type>
    typename std::enable_if<std::is_same<Node, PSNode>::value, Node *>::type
    nodeFactory(Args &&...args) {
        static_assert(expected_args_size<Type, sizeof...(args)>() ==
                              sizeof...(args),
                      "Incorrect number of arguments");
        return new Node(getNewNodeId(), Type, std::forward<Args>(args)...);
    }

  public:
    PointerGraph() {
        // nodes[0] represents invalid node (the node with id 0)
        nodes.emplace_back(nullptr);
        // the first several nodes are special nodes. For now, we just replace
        // them with nullptr, as those are created statically <-- FIXME!
        nodes.emplace_back(nullptr);
        nodes.emplace_back(nullptr);
        nodes.emplace_back(nullptr);
        assert(nodes.size() - 1 == PointerGraphReservedIDs::LAST_RESERVED_ID);
        initStaticNodes();
    }

    static void initStaticNodes();

    PointerSubgraph *createSubgraph(PSNode *root, PSNode *vararg = nullptr) {
        // NOTE: id of the subgraph is always index in _subgraphs + 1
        _subgraphs.emplace_back(new PointerSubgraph(
                static_cast<unsigned>(_subgraphs.size()) + 1, root, vararg));
        return _subgraphs.back().get();
    }

    template <PSNodeType Type, typename... Args>
    PSNode *create(Args &&...args) {
        PSNode *n = nodeFactory<Type>(std::forward<Args>(args)...);
        nodes.emplace_back(n); // C++17 returns a referece
        assert(n->getID() == nodes.size() - 1);
        return n;
    }

    // create a global node. Global nodes will be processed
    // in the same order in which they are created.
    // The global nodes are processed only once before the
    // analysis starts.
    template <PSNodeType Type, typename... Args>
    PSNode *createGlobal(Args &&...args) {
        PSNode *n = create<Type>(std::forward<Args>(args)...);
        _globals.push_back(n); // C++17 returns a referece
        assert(n->getID() == nodes.size() - 1);
        return n;
    }

    bool registerCall(PSNode *a, PSNode *b) { return callGraph.addCall(a, b); }

    GenericCallGraph<PSNode *> &getCallGraph() { return callGraph; }
    const GenericCallGraph<PSNode *> &getCallGraph() const { return callGraph; }
    const SubgraphsT &getSubgraphs() const { return _subgraphs; }

    const NodesT &getNodes() const { return nodes; }
    const GlobalNodesT &getGlobals() const { return _globals; }
    size_t size() const { return nodes.size() + _globals.size(); }

    void computeLoops();

    PointerGraph(PointerGraph &&) = default;
    PointerGraph &operator=(PointerGraph &&) = default;
    PointerGraph(const PointerGraph &) = delete;
    PointerGraph operator=(const PointerGraph &) = delete;

    PointerSubgraph *getEntry() const { return _entry; }
    void setEntry(PointerSubgraph *e);

    void remove(PSNode *nd);

    // get nodes in BFS order and store them into
    // the container
    template <typename ContainerOrNode>
    std::vector<PSNode *> getNodes(const ContainerOrNode &start,
                                   bool interprocedural = true,
                                   unsigned expected_num = 0) {
        ++dfsnum;

        std::vector<PSNode *> cont;
        if (expected_num != 0)
            cont.reserve(expected_num);

        struct DfsIdTracker {
            const unsigned dfsnum;
            DfsIdTracker(unsigned dnum) : dfsnum(dnum) {}

            void visit(PSNode *n) { n->dfsid = dfsnum; }
            bool visited(PSNode *n) const { return n->dfsid == dfsnum; }
        };

        // iterate over successors and call (return) edges
        struct EdgeChooser {
            const bool interproc;
            EdgeChooser(bool inter = true) : interproc(inter) {}

            void foreach (PSNode *cur, std::function<void(PSNode *)> Dispatch) {
                if (interproc) {
                    if (PSNodeCall *C = PSNodeCall::get(cur)) {
                        for (auto *subg : C->getCallees()) {
                            Dispatch(subg->root);
                        }
                        // we do not need to iterate over succesors
                        // if we dive into the procedure (as we will
                        // return via call return)
                        // NOTE: we must iterate over successors if the
                        // function is undefined
                        if (!C->getCallees().empty())
                            return;
                    } else if (PSNodeRet *R = PSNodeRet::get(cur)) {
                        for (auto *ret : R->getReturnSites()) {
                            Dispatch(ret);
                        }
                        if (!R->getReturnSites().empty())
                            return;
                    }
                }

                for (auto *s : cur->successors())
                    Dispatch(s);
            }
        };

        DfsIdTracker visitTracker(dfsnum);
        EdgeChooser chooser(interprocedural);
        BFS<PSNode, DfsIdTracker, EdgeChooser> bfs(visitTracker, chooser);

        bfs.run(start, [&cont](PSNode *n) { cont.push_back(n); });

        return cont;
    }
};

///
// get nodes reachable from n (including n),
// stop at node 'exit' (excluding) if not set to null
inline std::set<PSNode *> getReachableNodes(PSNode *n, PSNode *exit = nullptr,
                                            bool interproc = true) {
    ADT::QueueFIFO<PSNode *> fifo;
    std::set<PSNode *> cont;

    assert(n && "No starting node given.");
    fifo.push(n);

    while (!fifo.empty()) {
        PSNode *cur = fifo.pop();
        if (!cont.insert(cur).second)
            continue; // we already visited this node

        for (PSNode *succ : cur->successors()) {
            assert(succ != nullptr);

            if (succ == exit)
                continue;

            fifo.push(succ);
        }

        if (interproc) {
            if (PSNodeCall *C = PSNodeCall::get(cur)) {
                for (auto *subg : C->getCallees()) {
                    if (subg->root == exit)
                        continue;
                    fifo.push(subg->root);
                }
            } else if (PSNodeRet *R = PSNodeRet::get(cur)) {
                for (auto *ret : R->getReturnSites()) {
                    if (ret == exit)
                        continue;
                    fifo.push(ret);
                }
            }
        }
    }

    return cont;
}

} // namespace pta
} // namespace dg

#endif
