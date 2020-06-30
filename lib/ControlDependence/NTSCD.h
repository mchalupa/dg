#ifndef DG_NTSCD_H
#define DG_NTSCD_H

#include <vector>
#include <set>

#include "CDGraph.h"
#include "dg/ADT/Queue.h"

namespace dg {

class NTSCD {
    using ResultT = std::map<CDNode *, std::set<CDNode *>>;

    struct Info {
        unsigned colored{false};
        unsigned short counter;
    };

    std::unordered_map<CDNode*, Info> data;

    void compute(CDGraph& graph, CDNode *target) {

        // initialize nodes
        for (auto *nd : graph) {
            auto &D = data[nd];
            D.colored = false;
            D.counter = nd->successors().size();
        }

        // initialize the search
        data[target].colored = true;
        ADT::QueueLIFO<CDNode *> queue;
        queue.push(target);

        // search!
        while (!queue.empty()) {
            auto *node = queue.pop();
            assert(data[node].colored && "A non-colored node in queue");

            for (auto *pred : node->predecessors()) {
                auto& D = data[pred];
                --D.counter;
                if (D.counter == 0) {
                    D.colored = true;
                    queue.push(pred);
                }
            }
        }
    }

public:

    // returns control dependencies and reverse control dependencies
    std::pair<ResultT, ResultT> compute(CDGraph& graph) {
        ResultT CD;
        ResultT revCD;

        data.reserve(graph.size());

        for (auto *nd : graph) {
            compute(graph, nd);

            for (auto *predicate : graph.predicates()) {
                bool has_colored = false;
                bool has_uncolored = false;
                for (auto *succ : predicate->successors()) {
                    if (data[succ].colored)
                        has_colored = true;
                    if (!data[succ].colored)
                        has_uncolored = true;
                }

                if (has_colored && has_uncolored) {
                    CD[nd].insert(predicate);
                    revCD[predicate].insert(nd);
                }
            }
        }

        return {CD, revCD};
    }
};


class NTSCD2 {
    using ResultT = std::map<CDNode *, std::set<CDNode *>>;

    struct Info {
        unsigned color{0};
    };

    std::unordered_map<CDNode*, Info> data;

    void compute(CDGraph& graph, CDNode *target, ResultT& CD, ResultT& revCD) {
        std::set<CDNode *> frontier;
        std::set<CDNode *> new_frontier;

        // color the target node
        data[target].color = target->getID();
        for (auto *pred : target->predecessors()) {
            if (data[pred].color != target->getID()) {
                frontier.insert(pred);
            }
        }

        bool progress;
        do {
            progress = false;
            new_frontier.clear();

            for (auto *nd : frontier) {
                assert(!nd->successors().empty());
                // do all successors have the right color?
                bool colorit = true;
                for (auto *succ : nd->successors()) {
                    if (data[succ].color != target->getID()) {
                        colorit = false;
                        break;
                    }
                }

                // color the node and enqueue its predecessors
                if (colorit) {
                    data[nd].color = target->getID();
                    for (auto *pred : nd->predecessors()) {
                        if (data[pred].color != target->getID()) {
                            new_frontier.insert(pred);
                        }
                    }
                    progress = true;
                } else {
                    // re-queue the node as nothing happend
                    new_frontier.insert(nd);
                }
            }

            new_frontier.swap(frontier);
        } while (progress);

        for (auto *predicate : graph.predicates()) {
            bool has_colored = false;
            bool has_uncolored = false;
            for (auto *succ : predicate->successors()) {
                if (data[succ].color == target->getID())
                    has_colored = true;
                if (data[succ].color != target->getID())
                    has_uncolored = true;
            }

            if (has_colored && has_uncolored) {
                CD[target].insert(predicate);
                revCD[predicate].insert(target);
            }
        }
    }

public:

    // returns control dependencies and reverse control dependencies
    std::pair<ResultT, ResultT> compute(CDGraph& graph) {
        ResultT CD;
        ResultT revCD;

        data.reserve(graph.size());

        for (auto *nd : graph) {
            compute(graph, nd, CD, revCD);
        }

        return {CD, revCD};
    }
};


} // namespace dg

#endif // DG_NTSCD_H
