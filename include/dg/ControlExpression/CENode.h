#ifndef _DG_CE_NODE_H_
#define _DG_CE_NODE_H_

#include <list>
#include <set>
//#include <iostream>
#include <algorithm>
#include <cassert>

namespace dg {

enum class CENodeType {
        LABEL,
        SEQ,
        BRANCH,
        LOOP,
        EPS,
};

template <typename T> class CELabel;

class CENode {
    // to avoid RTTI
    CENodeType type;

public:
    // comparator for the Visits sets.
    // every element in that set is a label
    struct CECmp {
        bool operator()(const CENode *a, const CENode *b) const
        {
            assert(a->isa(CENodeType::LABEL));
            assert(b->isa(CENodeType::LABEL));

            return a->lt(b);
        }
    };

    using VisitsSetT = std::set<CENode *, CECmp>;

protected:
    CENode *parent;
    std::list<CENode *> children;

    VisitsSetT alwaysVisits;
    VisitsSetT sometimesVisits;

    CENode(CENodeType t) : type(t), parent(nullptr) {}

    void pruneSometimesVisits()
    {
        VisitsSetT diff;
        // do intersection with another child
        std::set_difference(sometimesVisits.begin(), sometimesVisits.end(),
                            alwaysVisits.begin(), alwaysVisits.end(),
                            std::inserter(diff, diff.end()), CECmp());

        // swap the intersection for alwaysVisit, so that we can
        // use it further
        diff.swap(sometimesVisits);
    }

public:
    class path_iterator {
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

        void moveUp()
        {
            assert(node->parent);

            // shift the iterator to the parent node
            iteratorReinit(node->parent);

            // we're at the end of the expression
            if (node == nullptr)
                return;

            if (node->isa(CENodeType::LOOP)) {
                // when the node is loop, we want it in the path,
                // since the loop is where the execution continues
                node = *it;
            } else if (node->parent && node->parent->isa(CENodeType::BRANCH)) {
                // is the node's parent a BRANCH? In that case
                // we must move up again
                moveUp();
            } else {
                // try to shift the iterator one position
                // to the right and check if we are not at
                // the end
                ++it;
                if (node->parent->children.end() == it)
                    // if we are again at the end, try it again
                    moveUp();
                else
                    node = *it;
            }
        }

    public:
        path_iterator(CENode *nd)
        {
            iteratorReinit(nd);
            // in the case that node was set to nullptr
            // here, we reset it back to nd, so that
            // we have valid begin() corresponding to 'this'
            node = nd;
        }

        path_iterator()
            : node(nullptr)
        {
        }

        bool operator==(const path_iterator& oth) const
        {
            return node == oth.node;
        }

        bool operator!=(const path_iterator& oth) const
        {
            return node != oth.node;
        }

        path_iterator& operator++()
        {
            if (node->parent) {
                // in SEQ and LOOPs we shift just to the right,
                // but in BRANCH we shift _up_
                if (node->parent->isa(CENodeType::BRANCH)) {
                        moveUp();
                } else {
                    ++it;

                    if (node->parent->children.end() == it)
                        moveUp();
                    else
                        node = *it;
                }
            } else
                node = nullptr;

            return *this;
        }

        path_iterator operator++(int)
        {
            path_iterator tmp = *this;
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

    path_iterator path_begin()
    {
        return path_iterator(this);
    }

    path_iterator path_end()
    {
        return path_iterator();
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

    CENode *getParent() const
    {
        return parent;
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

/*
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
        //std::cout << "<" << this << ">";
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
*/

    /*
     * simplify the CENode and its children
     * (recursively the whole subtree),
     * for example, move subsequent sequences to this
     * node, since it is the same (dot is the SEQ symbol):
     *      .            .
     *     / \         / | \
     *    .   C  =    A  B  C
     *   / \
     *  A   B
     */
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
            if ((*I)->type == CENodeType::SEQ &&
                (type == CENodeType::SEQ || type == CENodeType::LOOP)) {
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
            } else if ((*I)->type == CENodeType::SEQ && (*I)->children.size() == 1) {
                    // eliminate sequence when there is only one node in it

                    CENode *chld = *((*I)->children.begin());
                    new_children.push_back(chld);
                    // set the new parent
                    chld->parent = this;

                    // we over-took the child
                    // so clear the container, so that we won't
                    // delete the memory twice
                    (*I)->children.clear();

                    // now we can delete the memory
                    delete *I;
            } else if (type == CENodeType::SEQ && (*I)->type == CENodeType::EPS) {
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

#ifndef NDEBUG
        for (CENode *nd : children)
            assert(nd->parent == this);
#endif

    }

    // get the closest loop that is above this node
    CENode *getParentLoop() const
    {
        return [](const CENode *nd) -> CENode * {
            CENode *par = nd->getParent();
            if (par) {
                if (par->isa(CENodeType::LOOP))
                    return par;
                else
                    return par->getParentLoop();
            } else
                return nullptr;
        }(this);
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
        if (n->isa(CENodeType::LABEL))
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
        // We may skip this, but this way the computation
        // is easier.
        alwaysVisits.insert(this);
    }

    /*
    virtual void print() const override
    {
        std::cout << label;
    }
    */

};

class CESymbol: public CENode {
protected:
    CESymbol(CENodeType t)
        :CENode(t) {}
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
        assert(hasChildren() && "Sequence/loop has no children");

        // first recurse into children
        for (CENode *chld : children)
            chld->computeSets();

        // here we just make the union of children's
        // always and sometimes sets
        for (CENode *chld : children) {
            for (CENode *ch : chld->getAlwaysVisits())
                alwaysVisits.insert(ch);

            for (CENode *ch : chld->getSometimesVisits())
                if (alwaysVisits.count(ch) == 0)
                    sometimesVisits.insert(ch);
        }

        // delete the elements from sometimesVisits
        // that are in alwaysVisits. We tried to do it
        // while inserting, but there was a bug in STL,
        // so this is a workaround
        pruneSometimesVisits();
    }
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

        pruneSometimesVisits();
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

        // while computing sets, we suppose the loop
        // is always executed - the cases where it may
        // not be executed are solved later when browsing
        // the paths
        for (CENode *chld : children) {
            for (CENode *ch : chld->getAlwaysVisits())
                alwaysVisits.insert(ch);

            for (CENode *ch : chld->getSometimesVisits())
                if (alwaysVisits.count(ch) == 0)
                    sometimesVisits.insert(ch);
        }

        pruneSometimesVisits();
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

    /*
    virtual void print() const override
    {
        std::cout << "(e)";
    }
    */
};

} // namespace dg

#endif // _DG_CE_NODE_H_
