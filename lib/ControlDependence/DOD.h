#ifndef DG_DOD_H_
#define DG_DOD_H_

#include <map>
#include <set>

#include <dg/ADT/Queue.h>
#include <dg/ADT/SetQueue.h>
#include "CDGraph.h"

namespace dg {

class DODRanganath {
    //using ResultT = std::map<CDNode *, std::set<std::pair<CDNode *, CDNode *>>>;
    // NOTE: although DOD is a ternary relation, we treat it as binary
    // by breaking a->(b, c) to (a, b) and (a, c). It has no effect
    // on the results of slicing.
    using ResultT = std::map<CDNode *, std::set<CDNode *>>;
    enum class Color { WHITE, BLACK, UNCOLORED };

    struct Info {
        Color color{Color::UNCOLORED};
    };

    std::unordered_map<CDNode*, Info> data;

    void coloredDAG(CDGraph& graph, CDNode *n, std::set<CDNode *>& visited) {
        if (visited.insert(n).second) {
            auto& successors = n->successors();
            if (successors.empty())
                return;

            for (auto *q : successors) {
                coloredDAG(graph, q, visited);
            }
            auto *s = *(successors.begin());
            auto c = data[s].color;
            for (auto *q : successors) {
                if (data[q].color != c) {
                    c = Color::UNCOLORED;
                    break;
                }
            }
            data[n].color = c;
        }
    }

    bool dependence(CDNode *n, CDNode *m, CDNode *p, CDGraph& G) {
        for (auto *n : G) {
            data[n].color = Color::UNCOLORED;
        }
        data[m].color = Color::WHITE;
        data[p].color = Color::BLACK;

        std::set<CDNode *> visited;
        visited.insert(m);
        visited.insert(p);

        coloredDAG(G, n, visited);

        bool whiteChild = false;
        bool blackChild = false;

        for (auto *q : n->successors()) {
            if (data[q].color == Color::WHITE)
                whiteChild = true;
            if (data[q].color == Color::BLACK)
                blackChild = true;
        }

        return whiteChild && blackChild;
    }

    bool reachable(CDNode *from, CDNode *n) {
        ADT::SetQueue<ADT::QueueLIFO<CDNode *>> queue;
        queue.push(from);

        while (!queue.empty()) {
            auto *cur = queue.pop();
            if (n == cur) {
                return true;
            }

            for (auto *s : cur->successors()) {
                queue.push(s);
            }
        }
        return false;
    }

public:
    std::pair<ResultT, ResultT> compute(CDGraph& graph) {
        ResultT CD;
        ResultT revCD;

        DBG(cda, "Computing DOD (Ranganath)");

        data.reserve(graph.size());

        for (auto *n : graph.predicates()) {
            for (auto *m : graph) {
                for (auto *p : graph) {
                    if (p == m) {
                        continue;
                    }

                    if (reachable(m, p) && reachable(p, m) &&
                        dependence(n, p, m, graph)) {
                        //DBG(cda, "DOD: " << n->getID() << " -> {"
                        //                 << p->getID() << ", " << m->getID() << "}");
                        CD[m].insert(n);
                        CD[p].insert(n);
                        revCD[n].insert(m);
                        revCD[n].insert(n);
                    }
                }
            }
        }


        return {CD, revCD};
    }
};

};

#endif
