#ifndef _DG_POINTER_SUBGRAPH_H_
#define _DG_POINTER_SUBGRAPH_H_

#include "dg/ADT/Queue.h"
#include "dg/analysis/SubgraphNode.h"
#include "dg/analysis/PointsTo/PSNode.h"

#include <cassert>
#include <cstdarg>
#include <vector>
#include <memory>

namespace dg {
namespace analysis {
namespace pta {

void getNodes(std::set<PSNode *>& cont, PSNode *n, PSNode *exit, unsigned int dfsnum);

class PointerSubgraph
{
    unsigned int dfsnum;

    // root of the pointer state subgraph
    PSNode *root;

    using NodesT = std::vector<std::unique_ptr<PSNode>>;
    NodesT nodes;

    // Take care of assigning ids to new nodes
    unsigned int last_node_id = 0;
    unsigned int getNewNodeId() {
        return ++last_node_id;
    }

public:
    PointerSubgraph() : dfsnum(0), root(nullptr) {
        // nodes[0] represents invalid node (the node with id 0)
        nodes.emplace_back(nullptr);
    }

    const NodesT& getNodes() const { return nodes; }
    size_t size() const { return nodes.size(); }

    PointerSubgraph(PointerSubgraph&&) = default;
    PointerSubgraph& operator=(PointerSubgraph&&) = default;
    PointerSubgraph(const PointerSubgraph&) = delete;
    PointerSubgraph operator=(const PointerSubgraph&) = delete;

    PSNode *getRoot() const { return root; }
    void setRoot(PSNode *r) {
#if DEBUG_ENABLED
        bool found = false;
        for (auto& n : nodes) {
            if (n.get() == r) {
                found = true;
                break;
            }
        }
        assert(found && "The root lies outside of the graph");
#endif
        root = r;
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

    PSNode *create(PSNodeType t, ...) {
        va_list args;
        PSNode *node = nullptr;

        va_start(args, t);
        switch (t) {
            case PSNodeType::ALLOC:
            case PSNodeType::DYN_ALLOC:
                node = new PSNodeAlloc(getNewNodeId(), t);
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
                node = new PSNodeCall(getNewNodeId());
                break;
            default:
                node = new PSNode(getNewNodeId(), t, args);
                break;
        }
        va_end(args);

        assert(node && "Didn't created node");
        nodes.emplace_back(node);
        return node;
    }

    // get nodes in BFS order and store them into
    // the container
    std::vector<PSNode *> getNodes(PSNode *start_node,
                                   std::vector<PSNode *> *start_set = nullptr,
                                   unsigned expected_num = 0)
    {
        assert(root && "Do not have root");
        assert(!(start_set && start_node)
               && "Need either starting set or starting node, not both");

        ++dfsnum;
        ADT::QueueFIFO<PSNode *> fifo;

        if (start_set) {
            for (PSNode *s : *start_set) {
                fifo.push(s);
                s->dfsid = dfsnum;
            }
        } else {
            if (!start_node)
                start_node = root;

            fifo.push(start_node);
            start_node->dfsid = dfsnum;
        }

        std::vector<PSNode *> cont;
        if (expected_num != 0)
            cont.reserve(expected_num);

        while (!fifo.empty()) {
            PSNode *cur = fifo.pop();
            cont.push_back(cur);

            for (PSNode *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    fifo.push(succ);
                }
            }
        }

        return cont;
    }

};

inline void getNodes(std::set<PSNode *>& cont, PSNode *n, PSNode *exit, unsigned int dfsnum)
{
    // default behaviour is to enqueue all pending nodes
    ++dfsnum;
    ADT::QueueFIFO<PSNode *> fifo;

    assert(n && "No starting node given.");

    for (PSNode *succ : n->successors) {
        succ->dfsid = dfsnum;
        fifo.push(succ);
    }

    while (!fifo.empty()) {
        PSNode *cur = fifo.pop();
#ifndef NDEBUG
        bool ret = cont.insert(cur).second;
        assert(ret && "BUG: Tried to insert something twice");
#else
        cont.insert(cur);
#endif

        for (PSNode *succ : cur->successors) {
            if (succ == exit) continue;
            if (succ->dfsid != dfsnum) {
                succ->dfsid = dfsnum;
                fifo.push(succ);
            }
        }
    }
}

} // namespace pta
} // namespace analysis
} // namespace dg

#endif
