#ifndef DG_NTSCD_H
#define DG_NTSCD_H

#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "CDGraph.h"
#include "dg/ADT/Queue.h"
#include "dg/ADT/SetQueue.h"

namespace dg {

class NTSCD {
    using ResultT = std::map<CDNode *, std::set<CDNode *>>;

    struct Info {
        unsigned color{0};
    };

    std::unordered_map<CDNode *, Info> data;

    void compute(CDGraph &graph, CDNode *target, ResultT &CD, ResultT &revCD) {
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

        // iterate over frontier set, not over predicates -- only
        // the predicates that are in the frontier set may have colored
        // and uncolored successors
        for (auto *predicate : frontier) {
            if (!graph.isPredicate(*predicate))
                continue;
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
    std::pair<ResultT, ResultT> compute(CDGraph &graph) {
        ResultT CD;
        ResultT revCD;

        data.reserve(graph.size());

        for (auto *nd : graph) {
            compute(graph, nd, CD, revCD);
        }

        return {CD, revCD};
    }
};

class NTSCD2 {
    using ResultT = std::map<CDNode *, std::set<CDNode *>>;

    struct Info {
        unsigned colored{false};
        unsigned short counter;
    };

    std::unordered_map<CDNode *, Info> data;

    void compute(CDGraph &graph, CDNode *target) {
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
                auto &D = data[pred];
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
    std::pair<ResultT, ResultT> compute(CDGraph &graph) {
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

/// Implementation of the original algorithm for the computation of NTSCD
/// that is due to Ranganath et al. This algorithm is wrong and
/// can compute incorrect results (it behaves differently when
/// LIFO or FIFO or some other type of queue is used).
class NTSCDRanganath {
    using ResultT = std::map<CDNode *, std::set<CDNode *>>;

    // symbol t_{mn}
    struct Symbol : public std::pair<CDNode *, CDNode *> {
        Symbol(CDNode *a, CDNode *b) : std::pair<CDNode *, CDNode *>(a, b) {}
    };

    std::unordered_map<CDNode *, std::unordered_map<CDNode *, std::set<Symbol>>>
            S;

    ADT::SetQueue<ADT::QueueFIFO<CDNode *>> workbag;

    bool processNode(CDGraph &graph, CDNode *n) {
        bool changed = false;
        auto *s = n->getSingleSuccessor();
        if (s && s != n) { // (2.1) single successor case
            for (auto *p : graph.predicates()) {
                auto &Ssp = S[s][p];
                for (const Symbol &symb : S[n][p]) {
                    if (Ssp.insert(symb).second) {
                        DBG(tmp, "(1) S[" << s->getID() << ", " << p->getID()
                                          << "] <- t(" << symb.first->getID()
                                          << ", " << symb.second->getID()
                                          << ")");
                        DBG(tmp, "(2.1) queuing node: " << s->getID());
                        changed = true;
                        workbag.push(s);
                    }
                }
            }
        } else if (n->successors().size() >
                   1) { // (2.2) multiple successors case
            for (auto *m : graph) {
                auto &Smn = S[m][n];
                if (Smn.size() == n->successors().size()) {
                    for (auto *p : graph.predicates()) {
                        if (p == n) {
                            continue;
                        }
                        auto &Smp = S[m][p];
                        for (const Symbol &symb : S[n][p]) {
                            if (Smp.insert(symb).second) {
                                changed = true;
                                workbag.push(m);
                                DBG(tmp, "(1) S[" << m->getID() << ", "
                                                  << p->getID() << "] <- t("
                                                  << symb.first->getID() << ", "
                                                  << symb.second->getID()
                                                  << ")");
                                DBG(tmp, "(2.2) queuing node: " << m->getID());
                            }
                        }
                    }
                }
            }
        }

        return changed;
    }

  public:
    // returns control dependencies and reverse control dependencies
    // doFixpoint turns on the fix of the ranganath's algorithm
    // XXX: we should create a new fixed algorithm completely, as we do not need
    // the workbag and so on.
    std::pair<ResultT, ResultT> compute(CDGraph &graph,
                                        bool doFixpoint = true) {
        ResultT CD;
        ResultT revCD;

        S.reserve(2 * graph.predicates().size());

        // (1) initialize
        for (auto *p : graph.predicates()) {
            for (auto *n : p->successors()) {
                S[n][p].insert(Symbol{p, n});
                // DBG(tmp, "(1) S[" << n->getID() << ", " << p->getID()
                //                  << "] <- t(" << p->getID() << ", " <<
                //                  n->getID() << ")");
                // DBG(tmp, "(1) queuing node: " << n->getID());
                workbag.push(n);
            }
        }

        // (2) calculate all-path reachability
        if (doFixpoint) {
            DBG(cda, "Performing fixpoint of Ranganath's algorithm");
            bool changed;
            do {
                changed = false;
                for (auto *n : graph) {
                    changed |= processNode(graph, n);
                }
            } while (changed);
        } else {
            DBG(cda, "Running the original (wrong) Ranganath's algorithm");
            while (!workbag.empty()) {
                auto *n = workbag.pop();
                // DBG(cda, "Processing node: " << n->getID());
                processNode(graph, n);
            }
        }

        // (3) compute NTSCD
        for (auto *n : graph) {
            for (auto *p : graph.predicates()) {
                auto &Snp = S[n][p];

                // DBG(tmp, "(1) S[" << n->getID() << ", " << p->getID() << "]
                // ="); for (auto & symb : Snp) {
                //    DBG(tmp, "  t(" << symb.first->getID() << ", " <<
                //    symb.second->getID() << ")");
                //}
                if (!Snp.empty() && Snp.size() < p->successors().size()) {
                    CD[n].insert(p);
                    revCD[p].insert(n);
                }
            }
        }

        return {CD, revCD};
    }
};

} // namespace dg

#endif // DG_NTSCD_H
