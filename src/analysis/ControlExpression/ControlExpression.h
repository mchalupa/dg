#ifndef _DG_CONTROL_EXPRESSION_H_
#define _DG_CONTROL_EXPRESSION_H_

#include <list>
#include <vector>
#include <set>
#include <iostream>

enum CENodeType {
        LABEL  = 1,
        SEQ,
        BRANCH,
        LOOP,
        EPS,
};

class CENode {
    std::list<CENode *> childs;

    std::set<CENode *> alwaysVisits;
    std::set<CENode *> sometimesVisits;

    // to avoid RTTI
    CENodeType type;

protected:
    CENode(CENodeType t) : type(t) {}

public:
    void addChild(CENode *n)
    {
        childs.push_back(n);
    }

    // type identification and casting
    // to avoid RTTI. Later we could
    // add machinery like in LLVM
    // with cast<> and isa<> out of this class
    bool isa(CENodeType t) const
    {
        return t == type;
    }

    bool isLabel() const
    {
        return type == CENodeType::LABEL;
    }

    bool isSymbol() const
    {
        return type > CENodeType::LABEL;
    }

    virtual CENode *clone() const
    {
        return new CENode(*this);
    }

    // compute alwaysVisits and sometimesVisits sets
    //virtual void computeSets() = 0;

    virtual void print() const
    {
        std::cout << this;
    }

    void dump(int ind = 0) const
    {
        // not very effective
        auto mkind = [&ind]{for (int i = 0; i < ind; ++i) { std::cout << " "; }};

        mkind();
        switch(type) {
            case BRANCH:
                std::cout << "[+\n";
                for (auto chld : childs)
                    chld->dump(ind + 3);

                mkind();
                std::cout << "]\n";
                break;
            case LOOP:
                std::cout << "[*\n";
                for (auto chld : childs)
                    chld->dump(ind + 3);

                mkind();
                std::cout << "]\n";
                break;
            case SEQ:
                std::cout << "[seq\n";
                for (auto chld : childs)
                    chld->dump(ind + 3);

                mkind();
                std::cout << "]\n";
                break;
            default:
                print();
                std::cout << "\n";
        }
    }
};

template <typename T>
class CELabel: public CENode {
    T label;
public:
    CELabel<T>(const T& l)
        : CENode(CENodeType::LABEL), label(l) {}

    T& getLabel() const
    {
        return label;
    }

    virtual CENode *clone() const override
    {
        return new CELabel(*this);
    }

    /*
    virtual void computeSets() override
    {
    }
    */

    virtual void print() const override
    {
        std::cout << label;
    }

};

class CESymbol: public CENode {
protected:
    CESymbol(CENodeType t)
        :CENode(t) {}
};


class CEBranch: public CESymbol {
public:
    CEBranch(): CESymbol(CENodeType::BRANCH) {}
};

class CELoop: public CESymbol {
public:
    CELoop(): CESymbol(CENodeType::LOOP) {}
};

class CESeq: public CESymbol {
public:
    CESeq(): CESymbol(CENodeType::SEQ) {}
};

class CEEps: public CESymbol {
public:
    CEEps(): CESymbol(CENodeType::EPS) {}
};

template <typename T>
class CFANode {
public:
    typedef std::pair<CFANode<T> *, CENode *> EdgeT;


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
                if (I->second->isa(BRANCH)) {
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
        CENode *self_loop_label = makeOneSelfLoop();

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

                        // create new label
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
            edge.first->predecessors.erase(this);
        }

        successors.clear();
        predecessors.clear();
    }

    void dump() const
    {
        std::cout << " -- " << this << " --\n";
        for (auto E : successors) {
            std::cout << "   --> " << E.first << "\n";
            E.second->dump(5);
        }
    }

private:
    // merge self loops into one self loop
    // if there is more of them
    // \return pointer to the self-loop label or nullptr
    //         when there is no self-loop
    CENode *makeOneSelfLoop()
    {
        CENode *self_loop_label = nullptr;
        // there is not going to be much successors,
        // so go trough all of them and count self-loops
        unsigned count = 0;
        for (EdgeT& edge : successors)
            if (edge.first == this) {
                self_loop_label = edge.second;
                ++count;
            }

        // no multiple self-loop? We're done!
        if (count < 2)
            // if we have self_loop_label, then this
            // is the only self-loop label, otherwise
            // it is nullptr when there is no self-loop
            return self_loop_label;

        // we have at least two self-loops, so
        // gather them in branch
        CEBranch *br = new CEBranch();
        for (auto I = successors.begin(), E = successors.end();
             I != E;) {
             // if this is not a self-loop, continue
             if (I->first != this) {
                ++I;
                continue;
            }

            br->addChild(I->second->clone());
            auto tmp = I++;
            // erase this self-loop
            successors.erase(tmp);
        }

        addSuccessor(EdgeT(this, br));

        // we have a self-loop, return true
        return br;
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
class ControlExpression {
    CENode *root;
};


#endif // _DG_CONTROL_EXPRESSION_H_
