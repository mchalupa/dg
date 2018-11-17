#ifndef _SUBGRAPH_NODE_H_
#define _SUBGRAPH_NODE_H_

// This file defines a basis for nodes from
// PointerSubgraph and reaching definitions subgraph.

#ifndef NDEBUG
#include <iostream>
#endif // not NDEBUG

#include <vector>
#include <algorithm>

namespace dg {
namespace analysis {

namespace pta { class PSNode; }
namespace rd { class RDNode; }

template <typename NodeT,
          typename = std::enable_if<std::is_same<NodeT, pta::PSNode>::value ||
                                    std::is_same<NodeT, rd::RDNode>::value> >
class SubgraphNode {
    // id of the node. Every node from a graph has a unique ID;
    unsigned int id = 0;

    // data that can an analysis store in node
    // for its own needs
    void *data{nullptr};

    // data that can user store in the node
    // NOTE: I considered if this way is better than
    // creating subclass of PSNode and have whatever we
    // need in the subclass. Since AFAIK we need just this one pointer
    // at this moment, I decided to do it this way since it
    // is more simple than dynamic_cast... Once we need more
    // than one pointer, we can change this design.
    void *user_data{nullptr};

public:
    using NodesVec = std::vector<NodeT *>;

protected:
    // XXX: make those private!
    NodesVec successors;
    NodesVec predecessors;
    // XXX: maybe we could use SmallPtrVector or something like that
    NodesVec operands;
    // nodes that use this node
    NodesVec users;

    // size of the memory
    size_t size{0};

public:
    // FIXME: get rid of these things
    unsigned int dfs_id{0};
    unsigned int lowpt{0};

    // id of scc component
    unsigned int scc_id{0};
    // true if the node is on stack
    bool on_stack{false};

    SubgraphNode(unsigned id) : id(id) {}
#ifndef NDEBUG
    // in debug mode, we have virtual dump methods
    // and then we want virtual dtor. Otherwise,
    // we never use polymorphism for childern classes,
    // so we do not need the virtual dtor.
    virtual ~SubgraphNode() = default;
#endif

    unsigned int getID() const { return id; }

    void setSize(size_t s) { size = s; }
    size_t getSize() const { return size; }
    unsigned getSCCId() const { return scc_id; }

    // getters & setters for analysis's data in the node
    template <typename T>
    T* getData() { return static_cast<T *>(data); }
    template <typename T>
    const T* getData() const { return static_cast<T *>(data); }

    template <typename T>
    void *setData(T *newdata) {
        void *old = data;
        data = static_cast<void *>(newdata);
        return old;
    }

    // getters & setters for user's data in the node
    template <typename T>
    T* getUserData() { return static_cast<T *>(user_data); }
    template <typename T>
    const T* getUserData() const { return static_cast<T *>(user_data); }

    template <typename T>
    void *setUserData(T *newdata) {
        void *old = user_data;
        user_data = static_cast<void *>(newdata);
        return old;
    }

    NodeT *getOperand(int idx) const {
        assert(idx >= 0 && static_cast<size_t>(idx) < operands.size()
               && "Operand index out of range");

        return operands[idx];
    }

    void setOperand(int idx, NodeT *nd) {
        assert(idx >= 0 && static_cast<size_t>(idx) < operands.size()
               && "Operand index out of range");

        operands[idx] = nd;
    }

    size_t getOperandsNum() const {
        return operands.size();
    }

    void removeAllOperands() {
        for (auto o : operands) {
            o->removeUser(static_cast<NodeT *>(this));
        }
        operands.clear();
    }

    size_t addOperand(NodeT *n) {
        assert(n && "Passed nullptr as the operand");
        operands.push_back(n);
        n->addUser(static_cast<NodeT *>(this));
        assert(n->users.size() > 0);

        return operands.size();
    }

    bool hasOperand(NodeT *n) const {
        for (NodeT *x : operands) {
            if (x == n) {
                return true;
            }
        }

        return false;
    }

    void addSuccessor(NodeT *succ) {
        assert(succ && "Passed nullptr as the successor");
        successors.push_back(succ);
        succ->predecessors.push_back(static_cast<NodeT *>(this));
    }

    // return const only, so that we cannot change them
    // other way then addSuccessor()
    const NodesVec& getSuccessors() const { return successors; }
    const NodesVec& getPredecessors() const { return predecessors; }
    const NodesVec& getOperands() const { return operands; }
    const NodesVec& getUsers() const { return users; }

    void replaceSingleSuccessor(NodeT *succ) {
        assert(succ && "Passed nullptr as the successor");
        assert(successors.size() == 1);
        NodeT *old = successors[0];

        // we need to remove this node from
        // successor's predecessors
        std::vector<NodeT *> tmp;
        tmp.reserve(old->predecessorsNum());
        for (NodeT *p : old->predecessors) {
            if (p != this)
                tmp.push_back(p);
        }

        old->predecessors.swap(tmp);

        // replace the successor
        successors.clear();
        addSuccessor(succ);
    }

    // get the successor when we know there's only one of them
    NodeT *getSingleSuccessor() const {
        assert(successors.size() == 1);
        return successors.front();
    }

    // get the successor when there's only one of them,
    // otherwise get null
    NodeT *getSingleSuccessorOrNull() const {
        if (successors.size() == 1)
            return successors.front();

        return nullptr;
    }

    // get the predecessor when we know there's only one of them
    NodeT *getSinglePredecessor() const {
        assert(predecessors.size() == 1);
        return predecessors.front();
    }

    // get the predecessor when there's only one of them,
    // or get null
    NodeT *getSinglePredecessorOrNull() const {
        if (predecessors.size() == 1)
            return predecessors.front();

        return nullptr;
    }

    // insert this node in PointerSubgraph after n
    // this node must not be in any PointerSubgraph
    void insertAfter(NodeT *n) {
        assert(n && "Passed nullptr as the node");
        assert(predecessorsNum() == 0);
        assert(successorsNum() == 0);

        // take over successors
        successors.swap(n->successors);

        // make this node the successor of n
        n->addSuccessor(static_cast<NodeT *>(this));

        // replace the reference to n in successors
        for (NodeT *succ : successors) {
            for (unsigned i = 0; i < succ->predecessorsNum(); ++i) {
                if (succ->predecessors[i] == n)
                    succ->predecessors[i] = static_cast<NodeT *>(this);
            }
        }
    }

    // insert this node in PointerSubgraph before n
    // this node must not be in any PointerSubgraph
    void insertBefore(NodeT *n) {
        assert(n && "Passed nullptr as the node");
        assert(predecessorsNum() == 0);
        assert(successorsNum() == 0);

        // take over predecessors
        predecessors.swap(n->predecessors);

        // 'n' is a successors of this node
        addSuccessor(n);

        // replace the reference to n in predecessors
        for (NodeT *pred : predecessors) {
            for (unsigned i = 0; i < pred->successorsNum(); ++i) {
                if (pred->successors[i] == n)
                    pred->successors[i] = static_cast<NodeT *>(this);
            }
        }
    }

    // insert a sequence before this node in PointerSubgraph
    void insertSequenceBefore(std::pair<NodeT *, NodeT *>& seq) {
        assert(seq.first && seq.second && "Passed nullptr in the sequence");
        // the sequence must not be inserted in any PointerSubgraph
        assert(seq.first->predecessorsNum() == 0);
        assert(seq.second->successorsNum() == 0);

        // first node of the sequence takes over predecessors
        // this also clears 'this->predecessors' since seq.first
        // has no predecessors
        predecessors.swap(seq.first->predecessors);

        // replace the reference to 'this' in predecessors
        for (NodeT *pred : seq.first->predecessors) {
            for (unsigned i = 0; i < pred->successorsNum(); ++i) {
                if (pred->successors[i] == this)
                    pred->successors[i] = seq.first;
            }
        }

        // this node is successors of the last node in sequence
        seq.second->addSuccessor(this);
    }

    void isolate() {
        // Remove this node from successors of the predecessors
        for (NodeT *pred : predecessors) {
            std::vector<NodeT *> new_succs;
            new_succs.reserve(pred->successors.size());

            for (NodeT *n : pred->successors) {
                if (n != this)
                    new_succs.push_back(n);
            }

            new_succs.swap(pred->successors);
        }

        // remove this nodes from successors' predecessors
        for (NodeT *succ : successors) {
            std::vector<NodeT *> new_preds;
            new_preds.reserve(succ->predecessors.size());

            for (NodeT *n : succ->predecessors) {
                if (n != this)
                    new_preds.push_back(n);
            }

            new_preds.swap(succ->predecessors);
        }

        // Take every predecessor and connect it to every successor.
        for (NodeT *pred : predecessors) {
            for (NodeT *succ : successors) {
                assert(succ != this && "Self-loop");
                pred->addSuccessor(succ);
            }
        }

        successors.clear();
        predecessors.clear();
    }

    void replaceAllUsesWith(NodeT *nd, bool removeDupl = false) {
        assert(nd != this && "Replacing uses of 'this' with 'this'");

        // Replace 'this' in every user with 'nd'.
        for (NodeT *user : users) {
            for (int i = 0, e = user->getOperandsNum(); i < e; ++i) {
                if (user->getOperand(i) == this) {
                    user->setOperand(i, nd);
                    // register that 'nd' is now used in 'user'
                    nd->addUser(user);
                }
            }

            if (removeDupl)
                user->removeDuplicitOperands();
        }

        users.clear();
    }

    size_t predecessorsNum() const {
        return predecessors.size();
    }

    size_t successorsNum() const {
        return successors.size();
    }

#ifndef NDEBUG
    virtual void dump() const {
        std::cout << "SubgraphNode <" << getID();
    }

    virtual void print() const {
        dump();
        std::cout << "\n";
    }

    virtual void dumpv() const {
        print();
    }
#endif

private:
    bool removeDuplicitOperands() {
        std::set<NodeT *> ops;
        bool duplicated = false;
        for (auto op : getOperands()) {
            if (!ops.insert(op).second)
                duplicated = true;
        }

        if (duplicated) {
            operands.clear();
            operands.reserve(ops.size());
            // just push the new operads,
            // the users should not change in this case
            // (as we just remove the duplicated ones)
            for (auto op : ops)
                operands.push_back(op);
        }

        return duplicated;
    }

    void addUser(NodeT *nd) {
        // do not add duplicate users
        for (auto u : users)
            if (u == nd)
                return;

        users.push_back(nd);
    }

    void removeUser(NodeT *node) {
        using std::find;
        NodesVec &u = users;

        auto nodeToRemove = find(u.begin(), u.end(), node);
        if (nodeToRemove != u.end()) {
            u.erase(nodeToRemove);
        }
    }
};

} // analysis
} // dg
#endif // _SUBGRAPH_NODE_H_
