#ifndef DG_POINTER_GRAPH_H_
#define DG_POINTER_GRAPH_H_

#include "dg/ADT/Queue.h"
#include "dg/SubgraphNode.h"
#include "dg/CallGraph.h"
#include "dg/PointerAnalysis/PSNode.h"
#include "dg/BFS.h"
#include "dg/SCC.h"
#include "dg/util/debug.h"

#include <cassert>
#include <cstdarg>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace dg {
namespace pta {

// special nodes and pointers to them
extern PSNode *NULLPTR;
extern PSNode *UNKNOWN_MEMORY;
extern const Pointer NullPointer;
extern const Pointer UnknownPointer;

class PointerGraph;

// A single procedure in Pointer Graph
class PointerSubgraph {
    friend class PointerGraph;

    unsigned _id{0};

    PointerSubgraph(unsigned id, PSNode *r1, PSNode *va = nullptr)
        : _id(id), root(r1), vararg(va) {}

    PointerSubgraph() = default;
    PointerSubgraph(const PointerSubgraph&) = delete;

    // non-trivial strongly connected components
    bool _computed_loops{false};
    std::vector<std::vector<PSNode *>> _loops;
    std::unordered_map<const PSNode *, size_t> _node_to_loop;

public:
    PointerSubgraph(PointerSubgraph&&) = default;

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

    const std::vector<std::vector<PSNode *>>& getLoops() const {
        assert(_computed_loops && "Call computeLoops() first");
        return _loops;
    }

    const std::vector<std::vector<PSNode *>>& getLoops() {
        if (!computedLoops())
            computeLoops();
        return _loops;
    }

    // FIXME: remember just that a node is on loop, not the whole loops
    void computeLoops() {
        assert(root);
        assert(!computedLoops() && "computeLoops() called repeatedly");
        _computed_loops = true;

        DBG(pta, "Computing information about loops");

        // compute the strongly connected components
        auto SCCs = SCC<PSNode>().compute(root);
        for (auto& scc : SCCs) {
            if (scc.size() < 1)
                continue;
            // self-loop is also loop
            if (scc.size() == 1 &&
                scc[0]->getSingleSuccessorOrNull() != scc[0])
                continue;

            _loops.push_back(std::move(scc));
            assert(scc.empty() && "We wanted to move the scc");

            for (auto nd : _loops.back()) {
                assert(_node_to_loop.find(nd) == _node_to_loop.end());
                _node_to_loop[nd] = _loops.size() - 1;
            }
        }
    }
};


///
// Basic graph for pointer analysis
// -- contains CFG graphs for all procedures of the program.
class PointerGraph
{
    unsigned int dfsnum{0};

    // root of the pointer state subgraph
    // FIXME: this should be PointerSubgraph, not PSNode...
    PointerSubgraph *_entry{nullptr};

    using NodesT = std::vector<std::unique_ptr<PSNode>>;
    using SubgraphsT = std::vector<std::unique_ptr<PointerSubgraph>>;

    NodesT nodes;
    SubgraphsT _subgraphs;

    // Take care of assigning ids to new nodes
    unsigned int last_node_id = 0;
    unsigned int getNewNodeId() {
        return ++last_node_id;
    }

    GenericCallGraph<PSNode *> callGraph;

    void initStaticNodes() {
        NULLPTR->pointsTo.clear();
        UNKNOWN_MEMORY->pointsTo.clear();
        NULLPTR->pointsTo.add(Pointer(NULLPTR, 0));
        UNKNOWN_MEMORY->pointsTo.add(Pointer(UNKNOWN_MEMORY, Offset::UNKNOWN));
    }

    NodesT _globals;

    PSNode *_create(PSNodeType t, va_list args) {
        PSNode *node = nullptr;

        switch (t) {
            case PSNodeType::ALLOC:
                node = new PSNodeAlloc(getNewNodeId());
                break;
            case PSNodeType::GEP:
                node = new PSNodeGep(getNewNodeId(),
                                     va_arg(args, PSNode *),
                                     va_arg(args, Offset::type));
                break;
            case PSNodeType::MEMCPY:
                node = new PSNodeMemcpy(getNewNodeId(),
                                        va_arg(args, PSNode *),
                                        va_arg(args, PSNode *),
                                        va_arg(args, Offset::type));
                break;
            case PSNodeType::CONSTANT:
                node = new PSNode(getNewNodeId(), PSNodeType::CONSTANT,
                                  va_arg(args, PSNode *),
                                  va_arg(args, Offset::type));
                break;
            case PSNodeType::ENTRY:
                node = new PSNodeEntry(getNewNodeId());
                break;
            case PSNodeType::CALL:
                node = new PSNodeCall(t, getNewNodeId());
                break;
            case PSNodeType::CALL_FUNCPTR:
                node = new PSNodeCall(t, getNewNodeId());
                node->addOperand(va_arg(args, PSNode *));
                break;
            case PSNodeType::FORK:
                node = new PSNodeFork(getNewNodeId());
                node->addOperand(va_arg(args, PSNode *));
                break;
            case PSNodeType::JOIN:
                node = new PSNodeJoin(getNewNodeId());
                break;
            case PSNodeType::RETURN:
                node = new PSNodeRet(getNewNodeId(), args);
                break;
            case PSNodeType::CALL_RETURN:
                node = new PSNodeCallRet(getNewNodeId(), args);
                break;
            default:
                node = new PSNode(getNewNodeId(), t, args);
                break;
        }

        assert(node && "Didn't created node");
        return node;
    }

public:
    PointerGraph() {
        // nodes[0] represents invalid node (the node with id 0)
        nodes.emplace_back(nullptr);
        initStaticNodes();
    }

    PointerSubgraph *createSubgraph(PSNode *root,
                                    PSNode *vararg = nullptr) {
        // NOTE: id of the subgraph is always index in _subgraphs + 1
        _subgraphs.emplace_back(
            new PointerSubgraph(static_cast<unsigned>(_subgraphs.size()) + 1,
                                                      root, vararg));
        return _subgraphs.back().get();
    }

    PSNode *create(PSNodeType t, ...) {
        va_list args;

        va_start(args, t);
        PSNode *n = _create(t, args);
        va_end(args);

        nodes.emplace_back(n);
        return n;
    }

    // create a global node. Global nodes will be processed
    // in the same order in which they are created.
    // The global nodes are processed only once before the
    // analysis starts.
    PSNode *createGlobal(PSNodeType t, ...) {
        va_list args;

        va_start(args, t);
        PSNode *n = _create(t, args);
        va_end(args);

        _globals.emplace_back(n);
        return n;
    }

    bool registerCall(PSNode *a, PSNode *b) {
        return callGraph.addCall(a, b);
    }

    GenericCallGraph<PSNode *>& getCallGraph() { return callGraph; }
    const GenericCallGraph<PSNode *>& getCallGraph() const { return callGraph; }
    const SubgraphsT& getSubgraphs() const { return _subgraphs; }

    const NodesT& getNodes() const { return nodes; }
    const NodesT& getGlobals() const { return _globals; }
    size_t size() const { return nodes.size() + _globals.size(); }

    void computeLoops() {
        DBG(pta, "Computing information about loops for the whole graph");

        for (auto& it : _subgraphs) {
            if (!it->computedLoops())
                it->computeLoops();
        }
    }

    PointerGraph(PointerGraph&&) = default;
    PointerGraph& operator=(PointerGraph&&) = default;
    PointerGraph(const PointerGraph&) = delete;
    PointerGraph operator=(const PointerGraph&) = delete;

    PointerSubgraph *getEntry() const { return _entry; }
    void setEntry(PointerSubgraph *e) {
#if DEBUG_ENABLED
        bool found = false;
        for (auto& n : _subgraphs) {
            if (n.get() == e) {
                found = true;
                break;
            }
        }
        assert(found && "The entry is not a subgraph of the graph");
#endif
        _entry = e;
    }

    void remove(PSNode *nd) {
        assert(nd && "nullptr passed as nd");
        // the node must be isolated
        assert(nd->successors.empty() && "The node is still in graph");
        assert(nd->predecessors.empty() && "The node is still in graph");
        assert(nd->getID() < size() && "Invalid ID");
        assert(nd->getID() > 0 && "Invalid ID");
        assert(nd->users.empty() && "This node is used by other nodes");
        // if the node has operands, it means that the operands
        // have a reference (an user edge to this node).
        // We do not want to create dangling references.
        assert(nd->operands.empty() && "This node uses other nodes");
        assert(nodes[nd->getID()].get() == nd && "Inconsistency in nodes");

        // clear the nodes entry
        nodes[nd->getID()].reset();
    }

    // get nodes in BFS order and store them into
    // the container
    template <typename ContainerOrNode>
    std::vector<PSNode *> getNodes(const ContainerOrNode& start,
                                   bool interprocedural = true,
                                   unsigned expected_num = 0)
    {
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

            void foreach(PSNode *cur, std::function<void(PSNode *)> Dispatch) {
                if (interproc) {
                    if (PSNodeCall *C = PSNodeCall::get(cur)) {
                        for (auto subg : C->getCallees()) {
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
                        for (auto ret : R->getReturnSites()) {
                            Dispatch(ret);
                        }
                        if (!R->getReturnSites().empty())
                            return;
                    }
                }

                for (auto s : cur->getSuccessors())
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
inline std::set<PSNode *>
getReachableNodes(PSNode *n,
                  PSNode *exit = nullptr,
				  bool interproc = true)
{
    ADT::QueueFIFO<PSNode *> fifo;
    std::set<PSNode *> cont;

    assert(n && "No starting node given.");
    fifo.push(n);

    while (!fifo.empty()) {
        PSNode *cur = fifo.pop();
        if (!cont.insert(cur).second)
            continue; // we already visited this node

        for (PSNode *succ : cur->getSuccessors()) {
            assert(succ != nullptr);

            if (succ == exit)
                continue;

            fifo.push(succ);
        }

        if (interproc) {
            if (PSNodeCall *C = PSNodeCall::get(cur)) {
                for (auto subg : C->getCallees()) {
                    if (subg->root == exit)
                        continue;
                    fifo.push(subg->root);
                }
            } else if (PSNodeRet *R = PSNodeRet::get(cur)) {
                for (auto ret : R->getReturnSites()) {
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
