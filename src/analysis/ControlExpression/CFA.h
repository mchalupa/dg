#ifndef _DG_CE_CFA_H_
#define _DG_CE_CFA_H_

#include <list>
#include <vector>
#include <set>
//#include <iostream>

#include "CENode.h"
#include "ControlExpression.h"

namespace dg {

template <typename T>
class CFANode {
    T label;
public:
    using EdgeT = std::pair<CFANode<T> *, CENode *>;

    CFANode<T>(const T& l)
        :label(l) {}

    // move constructor
    CFANode<T>(CFANode<T>&& other)
        : label(std::move(other.label)),
          successors(std::move(other.successors)),
          predecessors(std::move(other.predecessors))
    {
    }

    // move assign operator
    CFANode<T>& operator=(CFANode<T>&& other)
    {
        other.successors.swap(successors);
        other.predecessors.swap(predecessors);
        label = std::move(other.label);
        return *this;
    }

    ~CFANode<T>()
    {
        for (EdgeT& edge : successors) {
            // delete the labels, we allocate them
            // using 'new'
            delete edge.second;
        }
    }

    // add a new successors - merge two successors
    // when they go to the same node
    void addSuccessor(EdgeT succ)
    {
        bool found = false;

        for (auto I = successors.begin(), E = successors.end();
             I != E; ++I) {
            // we already have an edge to this successor?
            if (I->first == succ.first) {
                // if the label already is a branch, just
                // add new branch to it (take over the ownership)
                if (I->second->isa(CENodeType::BRANCH)) {
                    I->second->addChild(succ.second);
                } else {
                    CEBranch *br = new CEBranch();
                    br->addChild(I->second);
                    br->addChild(succ.second);
                    // set the new label
                    I->second = br;
                }

                // we always have maximally one such successor
                found = true;
                break;
            }
        }

        if (!found) {
            successors.push_back(succ);
            succ.first->predecessors.insert(this);
        }
    }

    // simple helper that adds successors to a node
    // and sets the label for the node
    void addSuccessor(CFANode<T> *n)
    {
        addSuccessor(EdgeT(n, new CELabel<T>(n->label)));
    }

    const std::list<EdgeT>& getSuccessors() const
    {
        return successors;
    }

    void eliminate()
    {
        // entry or exit node should not be removed
        if (successors.empty() ||
            predecessors.empty())
                return;

        // merge multiple self-loops into one self
        // loop and get a flag if we have a self-loop.
        // When we have a self-loop, we must insert it
        // into the new labels
        CENode *self_loop_label = getSelfLoopLabel();

        // are we a node that has only self-loop, but no successor
        // or predecessor?
        if (successors.size() == 1 &&
            successors.begin()->first == this)
            return;

        for (CFANode<T> *pred : predecessors) {
            // skip self-loops, we must handle them
            // differently
            if (pred == this)
                continue;

            std::vector<EdgeT> new_edges;

            for (auto I = pred->successors.begin(),
                      E = pred->successors.end();
                 I != E;) {

                // is this the edge that points to this node?
                if (I->first == this) {
                    auto tmp = I++;

                    for (EdgeT& edge : successors) {
                        // do not add self-loops to this
                        // node, we're eliminating
                        if (edge.first == this)
                            continue;

                        // create a new label
                        CESeq *seq = new CESeq();
                        // add the label of the edge
                        seq->addChild(tmp->second->clone());

                        // if we have a self-loop, add it too
                        if (self_loop_label) {
                            CELoop *L = new CELoop();
                            L->addChild(self_loop_label->clone());
                            seq->addChild(L);
                        }

                        // and now add the label of the successor edge
                        seq->addChild(edge.second->clone());

                        // and add the new edge from the predecessor
                        // to the successor (now to the container,
                        // we add them later so that we won't corrupt
                        // the end() iterator)
                        new_edges.push_back(EdgeT(edge.first, seq));
                    }

                    // erase the old edge
                    delete tmp->second;
                    pred->successors.erase(tmp);
                } else {
                    // this is an edge that goes somewhere else
                    // that to this node, so just continue iterating
                    ++I;
                }
            }

            // add the new edges from predecessor to successors
            for (EdgeT& e : new_edges)
                pred->addSuccessor(e);
        }

        // erase this node from successors
        for (EdgeT& edge : successors) {
            delete edge.second; // delete the label
            edge.first->predecessors.erase(this);
        }

        // prevent double free of the labels
        successors.clear();
        predecessors.clear();
    }

    /*
    void dump() const
    {
        std::cout << " -- " << this << " --\n";
        for (auto E : successors) {
            std::cout << "   --> " << E.first << "\n";
            E.second->dump(5);
        }
    }
    */

    bool operator<(const CFANode<T>& oth) const
    {
        return label < oth.label;
    }

    bool hasSelfLoop() const
    {
        return predecessors.count(this) != 0;
    }


    bool hasSelfLoop()// const
    {
        return predecessors.count(this) != 0;
    }

    size_t successorsNum() const
    {
        return successors.size();
    }

    size_t predecessorsNum() const
    {
        return predecessors.size();
    }

    void print() const
    {
        for (const auto& s : successors)
            s.second->print();
    }

private:
    CENode *getSelfLoopLabel() const
    {
        for (const EdgeT& edge : successors)
            if (edge.first == this) {
                return edge.second;
            }

        return nullptr;
    }

    // we need both links, because
    // when eliminating, we must know what
    // edges go to this node. However,
    // for predecessors, we need only
    // the information that the predecessor
    // has an edge to this node, so set
    // of CENode * is OK
    std::list<EdgeT> successors;
    std::set<CFANode<T> *> predecessors;
};


template <typename T>
class CFA {
    CFANode<T> root;
    CFANode<T> end;

    // the CFA will have relativelly small number of nodes
    // in our case, so this kind of expensive comparsion should
    // not bring big overhead. On the other hand, it could
    // produce nicer control expressions
    /*
    struct NdsCmp {
        bool operator()(CFANode<T> *a, CFANode<T> *b) const
        {
            if (a->hasSelfLoop()) {
                if (b->hasSelfLoop()) {
                    return ((a->successorsNum() * a->predecessorsNum()) <
                            (b->successorsNum() * b->predecessorsNum()));
                } else
                    return false;
            } else {
                if (b->hasSelfLoop())
                    return true;
                else
                    return ((a->successorsNum() * a->predecessorsNum()) <
                            (b->successorsNum() * b->predecessorsNum()));

            }
        }
    };
    */

    std::set<CFANode<T> * /*, NdsCmp*/> nodes;

public:
    CFA<T>()
        :root(T()), end(T()) {}

    CFA<T>(CFA<T>&& oth)
        :root(std::move(oth.root)),
         end(std::move(oth.end)),
         nodes(std::move(oth.nodes))
    {
    }

    ~CFA<T>()
    {
        for (CFANode<T> *n : nodes)
            // delete the nodes,
            // we overtake the ownership
            // FIXME: use std::unique_ptr?
            delete n;
    }

    // add a node into CFA. The memory must
    // be allocated using 'new' operator and
    // it is over-taken and deleted on CFA's
    // destruction
    void addNode(CFANode<T> *n)
    {
        // if this node has no predecessors,
        // take it as a starting node
        if (n->predecessorsNum() == 0)
            root.addSuccessor(n);

        // if this node has no successors,
        // make it the exit node
        if (n->successorsNum() == 0)
            n->addSuccessor(typename CFANode<T>::EdgeT(&end, new CEEps()));

        nodes.insert(n);
    }

    CFANode<T>& getRoot()
    {
        return root;
    }

    ControlExpression compute()
    {
        // no starting point? Then we just choose one...
        if (root.successorsNum() == 0) {
            assert(false && "Not implemented yet");
            abort();// in the case of NDEBUG
        }

        // eliminate all the nodes
        for (CFANode<T> *nd : nodes)
            nd->eliminate();

        // we may have end-up with two nodes,
        // one of them having a self-loop above
        // it (if there was no end node) and the
        // other being the root.
        // There's no way how to eliminate the former,
        // so add a new end node that is going to be
        // a successor of it -- then we can eliminate it.
        //
        //               __r__
        //       l      |     |
        // root ----> (node)<-/
        //
        for (CFANode<T> *nd : nodes) {
            if (nd->hasSelfLoop()) {
                nd->addSuccessor(typename CFANode<T>::EdgeT(&end, new CEEps()));
                nd->eliminate();
            }
        }

        assert(root.successorsNum() == 1);
        CENode *expr = root.getSuccessors().begin()->second;
        expr->simplify();

        return ControlExpression(expr);
    }
};

} // namespace dg

#endif // _DG_CE_CFA_H_
