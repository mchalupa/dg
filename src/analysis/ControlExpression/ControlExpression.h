#ifndef _DG_CONTROL_EXPRESSION_H_
#define _DG_CONTROL_EXPRESSION_H_

#include <list>
#include <vector>
#include <set>
#include <iostream>
#include <algorithm>

enum CENodeType {
        LABEL  = 1,
        SEQ,
        BRANCH,
        LOOP,
        EPS,
};

template <typename T> class CELabel;

class CENode {
    // to avoid RTTI
    CENodeType type;

    template <typename T>
    void getLabels(const T& lab, std::vector<CENode *>& out)
    {
        if (isLabel()) {
         //   CELabel<T> *l = static_cast<CELabel<T> *>(this);
         //   std::cout << l->getLabel() << " == " << lab << "\n";
         //   asm("int3");
         //   if (l->getLabel() == lab) {
                out.push_back(this);
         //   }
        }

        // recursively call to children
        for (CENode *chld : children)
            chld->getLabels(lab, out);
    }

protected:
    CENode *parent;
    std::list<CENode *> children;

    // comparator for the Visits sets.
    // every element in that set is a label
    struct CECmp {
        bool operator()(const CENode *a, const CENode *b) const
        {
            assert(a->isa(LABEL));
            assert(b->isa(LABEL));

            return a->lt(b);
        }
    };

    CENode(CENodeType t) : type(t), parent(nullptr) {}

    typedef std::set<CENode *, CECmp> VisitsSetT;
    VisitsSetT alwaysVisits;
    VisitsSetT sometimesVisits;

public:
    class iterator {
        CENode *node;

        // iterator to the container that contains
        // this node. The invariant is that *it == node
        // or node == nullptr for end()
        std::list<CENode *>::iterator it;

        void iteratorReinit(CENode *nd)
        {
            if (nd && nd->parent) {
                it = std::find(nd->parent->children.begin(),
                               nd->parent->children.end(), nd);
                node = nd;
            } else
                node = nullptr;
        }

        void setNextNode()
        {
            assert(node->parent);

            // shift the iterator to the parent
            // node
            iteratorReinit(node->parent);

            if (node == nullptr)
                return;

            // try to shift the iterator one position
            // to the right and check if we are not at
            // the end
            ++it;
            if (node->parent->children.end() == it)
                // if we are again at the end, try it again
                setNextNode();
        }

    public:
        iterator(CENode *nd)
        {
            iteratorReinit(nd);
            // in the case that node was set to nullptr
            // here, we reset it back to nd, so that
            // we have valid begin() corresponding to 'this'
            node = nd;
        }

        iterator()
            : node(nullptr)
        {
        }

        bool operator==(const iterator& oth) const
        {
            return node == oth.node;
        }

        bool operator!=(const iterator& oth) const
        {
            return node != oth.node;
        }

        iterator& operator++()
        {
            if (node->parent) {
                ++it;
                if (node->parent->children.end() == it)
                    setNextNode();
                else
                    node = *it;
            } else
                node = nullptr;

            return *this;
        }

        iterator operator++(int)
        {
            iterator tmp = *this;
            operator++();
            return tmp;
        }

        CENode *operator*()
        {
            return node;
        }

        CENode **operator->()
        {
            return &node;
        }
    };

    iterator begin()
    {
        return iterator(this);
    }

    iterator end()
    {
        return iterator();
    }

    // return by value to allow using
    // move constructor
    template <typename T>
    std::vector<CENode *> getLabels(const T& lab)
    {
        std::vector<CENode *> tmp;
        getLabels(lab, tmp);
        std::cout << "SIZE: " << tmp.size() << "\n";
        return tmp;
    }

    virtual ~CENode()
    {
        for (CENode *n : children)
            delete n;
    }

    void setParent(CENode *p)
    {
        parent = p;
    }

    std::list<CENode *>& getChildren()
    {
        return children;
    }

    bool hasChildren() const
    {
        return !children.empty();
    }

    VisitsSetT& getAlwaysVisits()
    {
        return alwaysVisits;
    }

    VisitsSetT& getSometimesVisits()
    {
        return sometimesVisits;
    }

    void addChild(CENode *n)
    {
        children.push_back(n);
        n->parent = this;
    }

    // type identification and casting
    // to avoid RTTI. Later we could
    // add machinery like in LLVM
    // with cast<> and isa<> out of this class
    bool isa(CENodeType t) const
    {
        return t == type;
    }

    // less than operator, needed for
    // correct comparsion inside containers
    // when we want to compare according
    // to the label
    virtual bool lt(const CENode *n) const
    {
        // default
        return this < n;
    }

    bool isLabel() const
    {
        return type == CENodeType::LABEL;
    }

    bool isSymbol() const
    {
        return type > CENodeType::LABEL;
    }

    // recursively clone children
    // from 'this' to 'n'
    void cloneChildrenTo(CENode *n) const
    {
        std::list<CENode *> chlds;
        for (CENode *chld : children) {
            CENode *nch = chld->clone();
            nch->parent = n;
            chlds.push_back(nch);
        }

        n->children.swap(chlds);
    }

    virtual CENode *clone() const
    {
        // (we don't need it right now,
        // but it should be done)
        CENode *n = new CENode(*this);
        n->parent = nullptr;
        cloneChildrenTo(n);
        return n;
    }

    // compute alwaysVisits and sometimesVisits sets
    virtual void computeSets()
    {
        // don't want to do this an abstract method,
        // because we use this class also self-standingly
        assert(false && "This method must be overriden");
    }

    virtual void print() const
    {
        std::cout << this;
    }

    void dump(int ind = 0) const
    {
        // not very effective
        auto mkind = [&ind]{for (int i = 0; i < ind; ++i) { std::cout << " "; }};

        // DEBUG: do a check
        for (auto chld : children)
            assert(chld->parent == this);

        mkind();
        switch(type) {
            case BRANCH:
                std::cout << "[+\n";
                for (auto chld : children)
                    chld->dump(ind + 3);

                mkind();
                std::cout << "]";
                break;
            case LOOP:
                std::cout << "[*\n";
                for (auto chld : children)
                    chld->dump(ind + 3);

                mkind();
                std::cout << "]";
                break;
            case SEQ:
                std::cout << "[seq\n";
                for (auto chld : children)
                    chld->dump(ind + 3);

                mkind();
                std::cout << "]";
                break;
            case EPS:
                std::cout << "(e)\n";
                break;
            default:
                print();
        }

        if (!alwaysVisits.empty()) {
            std::cout << "   | ALWAYS: { ";
            for (CENode *ch : alwaysVisits) {
                ch->print();
                std::cout << " ";
            }
            std::cout << "}";
        }

        if (!sometimesVisits.empty()) {
            std::cout << " SMTM: { ";
            for (CENode *ch : sometimesVisits) {
                ch->print();
                std::cout << " ";
            }
            std::cout << "}";
        }

        std::cout << "\n";

    }

    // simplify the CENode and its children
    // (recursively the whole subtree),
    // for example, move subsequent sequences to this
    // node, since it is the same (dot is the SEQ symbol):
    //     .            .
    //    / \         / | \
    //   .   C  =    A  B  C
    //  / \
    // A   B
    void simplify()
    {
        // fist simplify the children
        if (!children.empty())
            for (CENode *chld : children)
                chld->simplify();

        // we'll create new container
        // and then we swap the contents
        std::list<CENode *> new_children;
        for (auto I = children.begin(), E = children.end();
             I != E; ++I) {
            // if this node is a SEQ node
            // and it has a child also SEQ,
            // just merge the child SEQ into
            // this node. Also, if this is a loop and
            // it contains SEQ, we can make the SEQ
            // just a children of the loop
            if ((*I)->type == SEQ &&
                (type == SEQ || type == LOOP)) {
                    // we over-take the children,
                    for (CENode *chld : (*I)->children) {
                        new_children.push_back(chld);
                        // set the new parent
                        chld->parent = this;
                    }
                    // we over-took the children
                    // so clear the container, so that we won't
                    // delete the memory twice
                    (*I)->children.clear();

                    // now we can delete the memory
                    delete *I;
            } else if (type == SEQ && (*I)->type == EPS) {
                // skip epsilons in SEQuences
                // (and since we drop the pointer,
                // release the memory)
                delete *I;
                continue;
            } else {
                // no change? so just copy the child
                assert((*I)->parent == this);
                new_children.push_back(*I);
            }
        }

        // put into children the newly computed children
        children.swap(new_children);

        // DEBUG, remove
        for (CENode *nd : children)
            assert(nd->parent == this);

    }
};

template <typename T>
class CELabel: public CENode {
    T label;
public:
    CELabel<T>(const T& l)
        : CENode(CENodeType::LABEL), label(l) {}

    const T& getLabel() const
    {
        return label;
    }

    virtual bool lt(const CENode *n) const override
    {
        if (n->isa(LABEL))
            return label < static_cast<const CELabel<T> *>(n)->label;
        else
            return this < n;
    }

    virtual CENode *clone() const override
    {
        assert(!hasChildren() && "A label has children");

        // this node do not have children,
        // so a shallow copy is OK
        CENode *n = new CELabel(*this);
        n->setParent(nullptr);
        return n;
    }

    virtual void computeSets() override
    {
        assert(!hasChildren() && "A label has children, whata?");
        assert(alwaysVisits.empty());
        assert(sometimesVisits.empty());
        // A label always just goes over itself.
        // We may skip this, but then the computation
        // is easier.
        alwaysVisits.insert(this);
    }

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

    virtual CENode *clone() const override
    {
        CENode *n = new CEBranch(*this);
        n->setParent(nullptr);
        cloneChildrenTo(n);
        return n;
    }

    virtual void computeSets() override
    {
        assert(alwaysVisits.empty());
        assert(sometimesVisits.empty());
        assert(hasChildren() && "Branch has no children");

        // first recurse into children
        for (CENode *chld : children)
            chld->computeSets();

        // get the labels that are in all branches
        // - we go over them no matter we do

        // initialize the alwaysVisits - copy the first child's
        // alwaysVisits
        auto I = children.begin();
        alwaysVisits = (*I)->getAlwaysVisits();
        ++I;
        for (auto E = children.end(); I != E; ++I) {
            VisitsSetT intersect;
            // do intersection with another child
            std::set_intersection(getAlwaysVisits().begin(), getAlwaysVisits().end(),
                                  (*I)->getAlwaysVisits().begin(),
                                  (*I)->getAlwaysVisits().end(),
                                  std::inserter(intersect, intersect.end()), CECmp());

            // swap the intersection for alwaysVisit, so that we can
            // use it further
            intersect.swap(alwaysVisits);
        }

        // compute the sometimesVisit
        for (CENode *chld : children) {
            for (CENode *ch : chld->getAlwaysVisits())
                if (getAlwaysVisits().count(ch) == 0)
                    getSometimesVisits().insert(ch);

            for (CENode *ch : chld->getSometimesVisits())
                if (getAlwaysVisits().count(ch) == 0)
                    getSometimesVisits().insert(ch);
        }
    }
};

class CELoop: public CESymbol {
public:
    CELoop(): CESymbol(CENodeType::LOOP) {}

    virtual CENode *clone() const override
    {
        CENode *n = new CELoop(*this);
        n->setParent(nullptr);
        cloneChildrenTo(n);
        return n;
    }

    virtual void computeSets() override
    {
        assert(alwaysVisits.empty());
        assert(sometimesVisits.empty());
        assert(hasChildren() && "Branch has no children");

        // first recurse into children
        for (CENode *chld : children)
            chld->computeSets();

        // there's always only a possibility that we
        // go into a loop, so this is just a union
        // of all the stuf into sometimesVisit

        for (CENode *chld : children) {
            for (CENode *ch : chld->getAlwaysVisits())
                sometimesVisits.insert(ch);

            for (CENode *ch : chld->getSometimesVisits())
                sometimesVisits.insert(ch);
        }
    }
};

class CESeq: public CESymbol {
public:
    CESeq(): CESymbol(CENodeType::SEQ) {}

    virtual CENode *clone() const override
    {
        CENode *n = new CESeq(*this);
        n->setParent(nullptr);
        cloneChildrenTo(n);
        return n;
    }

    virtual void computeSets() override
    {
        assert(alwaysVisits.empty());
        assert(sometimesVisits.empty());
        assert(hasChildren() && "Sequence has no children");

        // first recurse into children
        for (CENode *chld : children)
            chld->computeSets();

        // here we just make the union of children's
        // always and sometimes sets
        for (CENode *chld : children) {
            for (CENode *ch : chld->getAlwaysVisits())
                getAlwaysVisits().insert(ch);

            for (CENode *ch : chld->getSometimesVisits())
                if (getAlwaysVisits().count(ch) == 0)
                    getSometimesVisits().insert(ch);
        }
    }
};

class CEEps: public CESymbol {
public:
    CEEps(): CESymbol(CENodeType::EPS) {}

    virtual CENode *clone() const override
    {
        // we do not have children,
        // make a shallow copy
        CENode *n = new CEEps(*this);
        n->setParent(nullptr);
        return n;
    }

    virtual void print() const override
    {
        std::cout << "(e)";
    }
};

template <typename T>
class CFANode {
    T label;
public:
    typedef std::pair<CFANode<T> *, CENode *> EdgeT;

    CFANode<T>(const T& l)
        :label(l) {}

    // move constructor
    CFANode<T>(CFANode<T>&& other)
        : successors(std::move(other.successors)),
          predecessors(std::move(other.predecessors)),
          label(std::move(other.label))
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

    void dump() const
    {
        std::cout << " -- " << this << " --\n";
        for (auto E : successors) {
            std::cout << "   --> " << E.first << "\n";
            E.second->dump(5);
        }
    }

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
class ControlExpression {
    CFANode<T> root;
    CFANode<T> end;

    // the ControlExpression will have relativelly small number of nodes
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
    ControlExpression<T>()
        :root(T()), end(T()) {}

    void addNode(CFANode<T> *n)
    {
        // if this node has no predecessors,
        // take it as a starting node
        if (n->predecessorsNum() == 0)
            root.addSuccessor(n);

        nodes.insert(n);
    }

    CFANode<T>& getRoot()
    {
        return root;
    }

    CENode *compute()
    {
        // no starting point? Then we just choose one...
        if (root.successorsNum() == 0) {
            assert(false && "Not implemented yet");
            /*
            // set the new root
            root = std::move(*nodes.begin());
            // erase the node that is now invalid
            nodes.erase(nodes.begin());
            */
        }

        // until we don't support graphs without
        // any starting point, we can iterate until
        // the only one node lefts
        // // while (nodes.size() > 1) {
        /*
        while (nodes.size() > 0) {
            auto it = std::begin(nodes);
            // eliminate and delete the node
            (*it)->eliminate();
            nodes.erase(it);
        }
        */

        for (auto nd : nodes)
            nd->eliminate();

        // we may have end-up with
        // a one node having self-loop above
        // it, s there's no way how to eliminate
        // it. We must add a new
        //               __r__
        //       l      |     |
        // root ----> (node)<-/
        //
        for (auto nd : nodes) {
            if (nd->hasSelfLoop()) {
                nd->addSuccessor(typename CFANode<T>::EdgeT(&end, new CEEps()));    
                nd->eliminate();
            }
        }

        assert(root.successorsNum() == 1);
        CENode *expr = root.getSuccessors().begin()->second;
        expr->simplify();

        return expr;
    }
};


#endif // _DG_CONTROL_EXPRESSION_H_
