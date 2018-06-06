#ifndef _DG_POINTER_SUBGRAPH_H_
#define _DG_POINTER_SUBGRAPH_H_

#include <cassert>
#include <vector>
#include <cstdarg>

#include "ADT/Queue.h"
#include "analysis/SubgraphNode.h"
#include "PSNode.h"

namespace dg {
namespace analysis {
namespace pta {

void getNodes(std::set<PSNode *>& cont, PSNode *n, PSNode *exit, unsigned int dfsnum);

class PointerSubgraph
{
    unsigned int dfsnum;

    // root of the pointer state subgraph
    PSNode *root;

    unsigned int last_node_id = 0;
    std::vector<PSNode *> nodes;

public:
    ~PointerSubgraph() {
        for (PSNode *n : nodes)
            delete n;
    }

    PointerSubgraph() : dfsnum(0), root(nullptr) {
        nodes.reserve(128);
        // nodes[0] is nullptr (the node with id 0)
        nodes.push_back(nullptr);
    }

    const std::vector<PSNode *>& getNodes() const { return nodes; }
    size_t size() const { return nodes.size(); }

    PointerSubgraph(PointerSubgraph&&) = default;
    PointerSubgraph& operator=(PointerSubgraph&&) = default;
    PointerSubgraph(const PointerSubgraph&) = delete;
    PointerSubgraph operator=(const PointerSubgraph&) = delete;

    PSNode *getRoot() const { return root; }
    void setRoot(PSNode *r) {
#if DEBUG_ENABLED
        bool found = false;
        for (PSNode *n : nodes) {
            if (n == r) {
                found = true;
                break;
            }
        }
        assert(found && "The root lies outside of the graph");
#endif
        root = r;
    }

    void remove(PSNode *nd) {
        assert(nd);
        // the node must be isolated
        assert(nd->successors.empty());
        assert(nd->predecessors.empty());
        assert(nd->getID() < size());
        assert(nodes[nd->getID()] == nd && "Inconsistency in nodes");

        // clear the nodes entry
        nodes[nd->getID()] = nullptr;
        delete nd;
    }

    PSNode *create(PSNodeType t, ...) {
        va_list args;
        PSNode *node = nullptr;

        va_start(args, t);
        switch (t) {
            case PSNodeType::ALLOC:
            case PSNodeType::DYN_ALLOC:
                node = new PSNodeAlloc(++last_node_id, t);
                break;
            case PSNodeType::GEP:
                node = new PSNodeGep(++last_node_id,
                                     va_arg(args, PSNode *),
                                     va_arg(args, Offset::type));
                break;
            case PSNodeType::MEMCPY:
                node = new PSNodeMemcpy(++last_node_id,
                                        va_arg(args, PSNode *),
                                        va_arg(args, PSNode *),
                                        va_arg(args, Offset::type));
                break;
            case PSNodeType::CONSTANT:
                node = new PSNode(++last_node_id, PSNodeType::CONSTANT,
                                  va_arg(args, PSNode *),
                                  va_arg(args, Offset::type));
                break;
            case PSNodeType::ENTRY:
                node = new PSNodeEntry(++last_node_id);
                break;
            case PSNodeType::CALL:
                node = new PSNodeCall(++last_node_id);
                break;
            default:
                node = new PSNode(++last_node_id, t, args);
                break;
        }
        va_end(args);

        assert(node && "Didn't created node");
        nodes.push_back(node);
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
