#ifndef CD_CONTROL_CLOSURE_H_
#define CD_CONTROL_CLOSURE_H_

#include <set>
#include <vector>

#include "CDGraph.h"

#include "dg/ADT/Queue.h"
#include "dg/ADT/SetQueue.h"

namespace dg {

class StrongControlClosure {
    // this is basically the \Theta function from the paper
    template <typename Nodes, typename FunT>
    void foreachFirstReachable(const Nodes &nodes, CDNode *from,
                               const FunT &fun) {
        ADT::SetQueue<ADT::QueueLIFO<CDNode *>> queue;
        for (auto *s : from->successors()) {
            queue.push(s);
        }

        while (!queue.empty()) {
            auto *cur = queue.pop();
            if (nodes.count(cur) > 0) { // the node is from Ap?
                fun(cur);
            } else {
                for (auto *s : cur->successors()) {
                    queue.push(s);
                }
            }
        }
    }

    // this is the \Gamma function from the paper
    // (a bit different implementation)
    static std::set<CDNode *> gamma(CDGraph &graph,
                                    const std::set<CDNode *> &targets) {
        struct Info {
            unsigned colored{false};
            unsigned short counter;
        };

        std::unordered_map<CDNode *, Info> data;
        data.reserve(graph.size());
        ADT::QueueLIFO<CDNode *> queue;

        // initialize nodes
        for (auto *nd : graph) {
            auto &D = data[nd];
            D.colored = false;
            D.counter = nd->successors().size();
        }

        // initialize the search
        for (auto *target : targets) {
            data[target].colored = true;
            queue.push(target);
        }

        // search!
        while (!queue.empty()) {
            auto *node = queue.pop();
            assert(data[node].colored && "A non-colored node in queue");

            for (auto *pred : node->predecessors()) {
                auto &D = data[pred];
                --D.counter;
                if (D.counter == 0) {
                    D.colored = true;
                    queue.push(pred);
                }
            }
        }

        std::set<CDNode *> retval;
        for (auto *n : graph) {
            if (!data[n].colored) {
                retval.insert(n);
            }
        }
        return retval;
    }

    std::set<CDNode *> theta(const std::set<CDNode *> &X, CDNode *n) {
        std::set<CDNode *> retval;
        if (X.count(n) > 0) {
            retval.insert(n);
            return retval;
        }
        foreachFirstReachable(X, n, [&](CDNode *cur) { retval.insert(cur); });
        return retval;
    }

  public:
    using ValVecT = std::vector<CDNode *>;

    void closeSet(CDGraph &G, std::set<CDNode *> &X) {
        while (true) {
            ADT::SetQueue<ADT::QueueLIFO<CDNode *>> queue;
            for (auto *n : X) {
                for (auto *s : n->successors()) {
                    queue.push(s);
                }
            }

            CDNode *toadd = nullptr;
            while (!queue.empty()) {
                assert(toadd == nullptr);
                auto *p = queue.pop();
                assert(p && "popped nullptr");
                for (auto *r : p->successors()) {
                    assert(toadd == nullptr);
                    // DBG(cda, "Checking edge " << p->getID() << "->" <<
                    // r->getID());
                    // (a)
                    if (theta(X, r).size() != 1)
                        continue;

                    // (b)
                    auto gam = gamma(G, X);
                    if (gam.count(r) > 0)
                        continue;

                    // (c)
                    if (theta(X, p).size() < 2 && gam.count(p) == 0)
                        continue;

                    // all conditions met, we got our edge
                    // DBG(cda, "Found edge " << p->getID() << "->" <<
                    // r->getID());
                    assert(toadd == nullptr);
                    toadd = p;
                    break;
                }

                if (toadd)
                    break;

                for (auto *s : p->successors()) {
                    queue.push(s);
                }
            }

            if (toadd) {
                // DBG(cda, "Adding " << toadd->getID() << " to closure");
                X.insert(toadd);
                continue;
            } // no other edge to process
            break;
        }
    }

    ValVecT getClosure(CDGraph &G, const std::set<CDNode *> &nodes) {
        auto X = nodes;
        closeSet(G, X);
        return {X.begin(), X.end()};
    }
};

} // namespace dg

#endif
