#ifndef DG_DOD_H_
#define DG_DOD_H_

#include <map>
#include <unordered_map>
#include <set>
#include <functional>

#include <dg/ADT/Queue.h>
#include <dg/ADT/SetQueue.h>
#include <dg/ADT/Bitvector.h>

#include "CDGraph.h"

namespace dg {

// compute which nodes lie on all max paths from a given node
// (for all nodes). It is basically the same as NTSCD class
// but we remember the results of calls to compute()
class AllMaxPath {
public:
    using ResultT = std::map<CDNode *, const ADT::SparseBitvector&>;

private:
    struct Info {
        ADT::SparseBitvector colors;
        unsigned short counter;
    };

    std::unordered_map<CDNode*, Info> data;

    void compute(CDGraph& graph, CDNode *target) {

        // initialize nodes
        for (auto *nd : graph) {
            data[nd].counter = nd->successors().size();
        }

        // initialize the search
        data[target].colors.set(target->getID());
        ADT::QueueLIFO<CDNode *> queue;
        queue.push(target);

        // search!
        while (!queue.empty()) {
            auto *node = queue.pop();
            assert(data[node].colors.get(target->getID()) && "A non-colored node in queue");

            for (auto *pred : node->predecessors()) {
                auto& D = data[pred];
                --D.counter;
                if (D.counter == 0) {
                    D.colors.set(target->getID());
                    queue.push(pred);
                }
            }
        }
    }

public:

    // returns mapping CDNode -> Set of CDNodes (where the set is implemented as a bitvector)
    ResultT compute(CDGraph& graph) {
        ResultT res;

        data.reserve(graph.size());

        for (auto *nd : graph) {
            compute(graph, nd);
            res.emplace(nd, data[nd].colors);
        }

        return res;
    }
};


class DOD {
public:
    //using ResultT = std::map<CDNode *, std::set<std::pair<CDNode *, CDNode *>>>;
    // NOTE: although DOD is a ternary relation, we treat it as binary
    // by breaking a->(b, c) to (a, b) and (a, c). It has no effect
    // on the results of slicing.
    using ResultT = std::map<CDNode *, std::set<CDNode *>>;
    using ColoringT = ADT::SparseBitvector;

private:
    struct ColoredAp {
        CDGraph Ap;
        ColoringT blues;
        ColoringT reds;

        // mapping G -> Ap
        std::unordered_map<CDNode *, CDNode *> _mapping{};
        // mapping Ap -> G
        std::unordered_map<CDNode *, CDNode *> _rev_mapping{};

        CDNode *createNode(CDNode *gnode) {
            auto *nd = &Ap.createNode();
            _mapping[gnode] = nd;
            _rev_mapping[nd] = gnode;
            return nd;
        }

        CDNode *getNode(CDNode *gnode) {
            auto it = _mapping.find(gnode);
            return it == _mapping.end() ? nullptr : it->second;
        }

        CDNode *getGNode(CDNode *apnode) {
            auto it = _rev_mapping.find(apnode);
            return it == _rev_mapping.end() ? nullptr : it->second;
        }

        ColoredAp() = default;
        ColoredAp(CDGraph&& g, ColoringT&& b, ColoringT&& r)
            : Ap(std::move(g)), blues(std::move(b)), reds(std::move(r)) {}
        ColoredAp(ColoredAp&&) = default;
        ColoredAp(const ColoredAp&) = delete;

        bool isBlue(CDNode *n) const { return blues.get(n->getID()); }
        bool isRed(CDNode *n) const { return reds.get(n->getID()); }
    };

    template <typename Nodes, typename FunT>
    void foreachFirstReachable(const Nodes& nodes,
                               CDNode *from,
                               const FunT& fun) {
        // FIXME: this breaks the complexity (it uses std::set)
        ADT::SetQueue<ADT::QueueLIFO<CDNode *>> queue;
        for (auto *s : from->successors()) {
            queue.push(s);
        }

        while (!queue.empty()) {
            auto *cur = queue.pop();
            if (nodes.get(cur->getID())) { // the node is from Ap?
                fun(cur);
            } else {
                for (auto *s : cur->successors()) {
                    queue.push(s);
                }
            }
        }
    }

    // Create the Ap graph (nodes and edges)
    ColoredAp createAp(const ADT::SparseBitvector& nodes, CDGraph& graph, CDNode *node) {
        ColoredAp CAp;
        CDGraph& Ap = CAp.Ap;

        // create nodes of graph
        for (auto *n : graph) {
            if (nodes.get(n->getID())) {
                CAp.createNode(n);
                //DBG(cda, "  - Ap has node " << n->getID() << " (which is " << nd->getID() << " in Ap)");
            }
        }

        assert(CAp.getNode(node) != nullptr);

        if (CAp.Ap.size() < 3) {
            return {};   // no DOD possible, bail out early
        }

        // Add edges. FIXME: we can use a better implementation
        for (auto *n : Ap) {
            auto *gn = CAp.getGNode(n);

            foreachFirstReachable(nodes, gn, [&](CDNode *cur) {
                auto *apn = CAp.getNode(cur);
                assert(apn);
                n->addSuccessor(apn);
                //DBG(cda, "  - Ap edge " << gn->getID() << " -> " << cur->getID());
            });
        }

        assert(CAp.getNode(node) && "node is not in Ap");
        if (CAp.getNode(node)->successors().size() < 2) {
            return {}; // no DOD possible, skip the rest of building colored Ap
        }

        return CAp;
    }

    // create the Ap graph (calls createAp) and color the nodes in the Ap graph
    ColoredAp createColoredAp(AllMaxPath::ResultT& allpaths, CDGraph& graph, CDNode *node) {
        auto it = allpaths.find(node);
        if (it == allpaths.end()) {
            return {};
        }
        const auto& nodes = it->second;

        ColoredAp CAp = createAp(nodes, graph, node);
        if (CAp.Ap.empty()) {
            return {};
        }

        // initialize the colors
        assert(node->successors().size() == 2
               && "Node is not the right predicate");

        // color nodes
        auto& succs = node->successors();
        auto sit = succs.begin();
        CDNode *bluesucc = *sit;
        CDNode *redsucc = *(++sit);
       //DBG(cda, "Blue successor: " << bluesucc->getID());
       //DBG(cda, "Red successor: " << redsucc->getID());
        assert(++sit == succs.end());

        if (nodes.get(bluesucc->getID())) { // is blue successor in Ap?
            auto *apn = CAp.getNode(bluesucc);
            assert(apn);
            CAp.blues.set(apn->getID());
            //DBG(cda, "  - Ap blue: " << apn->getID() << " (" << bluesucc->getID() << " in original)");
        } else {
            foreachFirstReachable(nodes, bluesucc, [&](CDNode *cur) {
                auto *apn = CAp.getNode(cur);
                assert(apn);
                CAp.blues.set(apn->getID());
                //DBG(cda, "  - Ap blue: " << apn->getID() << " (" << cur->getID() << " in original)");
            });
        }

        if (nodes.get(redsucc->getID())) { // is red successor in Ap?
            auto *apn = CAp.getNode(redsucc);
            assert(apn);
            CAp.reds.set(apn->getID());
            //DBG(cda, "  - Ap red: " << redsucc->getID());
        } else {
            foreachFirstReachable(nodes, redsucc, [&](CDNode *cur) {
                auto *apn = CAp.getNode(cur);
                assert(apn);
                CAp.reds.set(apn->getID());
                //DBG(cda, "  - Ap red: " << apn->getID() << " (" << cur->getID() << " in original)");
            });
        }

        return CAp;
    }

    static bool checkAp(CDGraph& Ap) {
        // we can have only a single node with multiple successors
        CDNode *p = nullptr;
        for (auto *n : Ap) {
            if (p) {
                assert(n->successors().size() == 1);
                assert(n->getSingleSuccessor() != n);
            } else {
                if (n->successors().size() > 1) {
                    p = n;
                }
            }
        }
        assert(p && "No entry node of Ap");

        // from the p node there are edges that go into a cycle
        // that contains the rest of the nodes
        std::set<CDNode *> visited;
        auto *n = *(p->successors().begin());
        auto *cur = n;
        do {
            assert(cur != p);

            bool notseen = visited.insert(cur).second;
            assert(notseen && "Visited a node twice");

            cur = cur->getSingleSuccessor();
            assert(cur && "Node on the cycle does not have a unique successor");
        } while (cur != n);

        assert(visited.size() == Ap.size() - 1 && "Cycle does not contain all the nodes except p");
        return true;
    }

    void computeDOD(ColoredAp& CAp, CDNode *p, ResultT& CD, ResultT& revCD,
                    bool asTernary = true) {
        assert(checkAp(CAp.Ap)); // sanity check

       //for (auto *nd : CAp.Ap) {
       //      DBG(tmp, ">> Ap: " << nd->getID() << " b: " << CAp.blues.get(nd->getID())
       //                                        << " r: " << CAp.reds.get(nd->getID()));
       //}
       //for (auto *nd : CAp.Ap) {
       //    for (auto *succ: nd->successors()) {
       //      DBG(tmp, ">> Ap:   " << nd->getID() << " -> " << succ->getID());
       //    }
       //}

        CDNode *startB = nullptr, *endB = nullptr;
        CDNode *startR = nullptr, *endR = nullptr;

        // get some blue node to have a starting point
        auto bid = *(CAp.blues.begin());
        //DBG(tmp, "Blue node with id " << bid);
        startB = CAp.Ap.getNode(bid);
        assert(startB && "The blue node is not on Ap");
        //DBG(tmp, "Starting from blue: " << startB->getID());
        assert(CAp.isBlue(startB));

        auto *cur = startB;
        // go around the cycle, check the structure and find the borders of the colored nodes

        // start searching the blue prefix
        do {
            if (CAp.isBlue(cur)) {
                endB = cur;
            }
            if (CAp.isRed(cur)) {
                startR = cur;
                break;
            }

            cur = cur->getSingleSuccessor();
            assert(cur && "A node on cycle has no unique successor");
        } while (cur != startB);

        assert(endB && "Do not have (temporary) blue end");
        assert(startR && "Did not find a red node");
       //if (startB == startR) {
       //    return; // no DOD
       //}

        // now the red sequence
        while (cur != startB) {
            if (CAp.isRed(cur)) {
                endR = cur;
            }

            if (CAp.isBlue(cur)) {
                // check the possible blue suffix
                // (there can be none red node)
                auto *ncur = cur;
                while (ncur != startB) {
                    if (CAp.isRed(ncur)) {
                        return; // No DOD possible
                    }

                    ncur = ncur->getSingleSuccessor();
                    assert(ncur && "A node on cycle has no unique successor");
                }
                endB = startB;
                startB = cur;
                break;
            }

            cur = cur->getSingleSuccessor();
            assert(cur && "A node on cycle has no unique successor");
        };

        assert(startB);
        assert(endB);
        assert(startR);
        assert(endR);

        if (asTernary) {
             cur = endB;
             do {
                 auto *gcur = CAp.getGNode(cur);
                 assert(gcur);

                 auto *ncur = endR;
                 do {
                     auto *gncur = CAp.getGNode(ncur);
                     assert(gncur);
                    //DBG(cda, p->getID() << " - dod -> {" << gcur->getID() << ", "
                    //                                     << gncur->getID() << "}");

                     CD[gcur].insert(p);
                     CD[gncur].insert(p);
                     revCD[p].insert(gcur);
                     revCD[p].insert(gncur);

                     ncur = ncur->getSingleSuccessor();
                 } while (!(CAp.isBlue(ncur) || CAp.isRed(ncur)));

                 cur = cur->getSingleSuccessor();
             } while (!(CAp.isBlue(cur) || CAp.isRed(cur)));
        } else { // break into binary relation
            cur = endB;
            do {
                auto *gcur = CAp.getGNode(cur);
                assert(gcur);
                CD[gcur].insert(p);
                revCD[p].insert(gcur);
                //DBG(cda, p->getID() << " - dod -> " << gcur->getID());
                cur = cur->getSingleSuccessor();
            } while (!(CAp.isBlue(cur) || CAp.isRed(cur)));
            cur = endR;
            do {
                auto *gcur = CAp.getGNode(cur);
                assert(gcur);
                CD[gcur].insert(p);
                revCD[p].insert(gcur);
                //DBG(cda, p->getID() << " - dod -> " << gcur->getID());
                cur = cur->getSingleSuccessor();
            } while (!(CAp.isBlue(cur) || CAp.isRed(cur)));
        }
    }

protected:
    // make this public, so that we can use it in NTSCD+DOD algorithm
    template <typename OnAllPaths>
    void computeDOD(CDNode *p, CDGraph& graph,
                    OnAllPaths& allpaths,
                    ResultT& CD, ResultT& revCD) {
        assert(p->successors().size() == 2 && "We work with at most 2 successors");

        DBG_SECTION_BEGIN(cda, "Creating Ap graph for fun " << graph.getName() <<
                               " node " << p->getID());
        auto res = createColoredAp(allpaths, graph, p);
        DBG_SECTION_END(cda, "Done creating Ap graph");
        if (res.Ap.empty()) {
            DBG(cda, "No DOD in the Ap are possible");
            // no DOD possible
            return;
        }

        DBG(cda, "Computing DOD from the Ap");
        computeDOD(res, p, CD, revCD);
    }

public:

    std::pair<ResultT, ResultT> compute(CDGraph& graph) {
        ResultT CD;
        ResultT revCD;

        DBG_SECTION_BEGIN(cda, "Computing DOD for all predicates");

        AllMaxPath allmaxpath;
        DBG_SECTION_BEGIN(cda, "Coputing nodes that are on all max paths from nodes for fun "
                                << graph.getName());
        auto allpaths = allmaxpath.compute(graph);
        DBG_SECTION_END(cda, "Done coputing nodes that are on all max paths from nodes");

        for (auto *p : graph.predicates()) {
            computeDOD(p, graph, allpaths, CD, revCD);
        }

        DBG_SECTION_END(cda, "Finished computing DOD for all predicates");
        return {CD, revCD};
    }
};


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

    // the paper uses 'reachable', but it is wrong
    // Keep the method for now anyway.
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

    bool onallpaths(CDNode *from, CDNode *n, CDGraph& G) {
        if (from == n) // fast path
            return true;

        struct NodeInf { bool onstack{false}; bool visited{false}; };
        std::unordered_map<CDNode *, NodeInf> data;

        data.reserve(G.size());

        const std::function<bool(CDNode*, CDNode*)> onallpths = [&](CDNode *node, CDNode *target) -> bool {
            if (node == target)
                return true;

            data[node].visited = true;

            if (!node->hasSuccessors())
                return false;

            for (auto *s : node->successors()) {
                if (data[s].onstack)
                    return false;
                if (!data[s].visited) {
                    data[s].onstack = true;
                    if (!onallpths(s, target))
                        return false;
                    data[s].onstack = false;
                }
            }
            // if we have successors and got here,
            // then all successors reach target
            return true;
        };

        data[from].onstack = true;
        //DBG(tmp, "on all paths:" << from->getID() << ", " << n->getID() << ": " << r );
        return onallpths(from, n);
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

                    if (onallpaths(m, p, graph) && onallpaths(p, m, graph) &&
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
