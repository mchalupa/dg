#ifndef SUBGRAPH_NODE_H_
#define SUBGRAPH_NODE_H_

// This file defines a basis for nodes from
// PointerGraph and reaching definitions subgraph.

#ifndef NDEBUG
#include <iostream>
#endif // not NDEBUG

#include <algorithm>
#include <cassert>
#include <set>
#include <vector>

#include <dg/util/iterators.h>

namespace dg {

namespace pta {
class PSNode;
}
namespace dda {
class RWNode;
}

template <typename NodeT>
class SubgraphNode {
  public:
    using IDType = unsigned;
    using NodesVec = std::vector<NodeT *>;

  private:
    // id of the node. Every node from a graph has a unique ID;
    IDType id = 0;

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

    // id of scc component
    unsigned int scc_id{0};

  protected:
    // XXX: make those private!
    NodesVec _successors;
    NodesVec _predecessors;
    // XXX: maybe we could use SmallPtrVector or something like that
    NodesVec operands;
    // nodes that use this node
    NodesVec users;

    // size of the memory
    size_t size{0};

  public:
    SubgraphNode(IDType id) : id(id) {}
#ifndef NDEBUG
    // in debug mode, we have virtual dump methods
    // and then we want virtual dtor. Otherwise,
    // we never use polymorphism for childern classes,
    // so we do not need the virtual dtor.
    virtual ~SubgraphNode() = default;
#endif

    IDType getID() const { return id; }

    void setSize(size_t s) { size = s; }
    size_t getSize() const { return size; }
    void setSCCId(unsigned id) { scc_id = id; }
    unsigned getSCCId() const { return scc_id; }

    // getters & setters for analysis's data in the node
    template <typename T>
    T *getData() {
        return static_cast<T *>(data);
    }
    template <typename T>
    const T *getData() const {
        return static_cast<T *>(data);
    }

    template <typename T>
    void *setData(T *newdata) {
        void *old = data;
        data = static_cast<void *>(newdata);
        return old;
    }

    // getters & setters for user's data in the node
    template <typename T>
    T *getUserData() {
        return static_cast<T *>(user_data);
    }
    template <typename T>
    const T *getUserData() const {
        return static_cast<T *>(user_data);
    }

    template <typename T>
    void *setUserData(T *newdata) {
        void *old = user_data;
        user_data = static_cast<void *>(newdata);
        return old;
    }

    NodeT *getOperand(size_t idx) const {
        assert(idx < operands.size() && "Operand index out of range");

        return operands[idx];
    }

    void setOperand(size_t idx, NodeT *nd) {
        assert(idx < operands.size() && "Operand index out of range");

        operands[idx] = nd;
    }

    size_t getOperandsNum() const { return operands.size(); }

    void removeAllOperands() {
        for (auto *o : operands) {
            o->removeUser(static_cast<NodeT *>(this));
        }
        operands.clear();
    }

    template <typename NodePtr, typename... Args>
    size_t addOperand(NodePtr node, Args &&...args) {
        addOperand(node);
        return addOperand(std::forward<Args>(args)...);
    }

    template <typename NodePtr,
              typename Node_plain = typename std::remove_pointer<NodePtr>::type>
    size_t addOperand(NodePtr n) {
        static_assert(std::is_pointer<NodePtr>::value &&
                              std::is_base_of<NodeT, Node_plain>::value,
                      "Argument is not a pointer or is not derived from this "
                      "class.");
        assert(n && "Passed nullptr as the operand");
        operands.push_back(n);
        n->addUser(static_cast<NodeT *>(this));
        assert(!n->users.empty());

        return operands.size();
    }

    bool hasOperand(NodeT *n) const {
        return dg::any_of(operands, [n](NodeT *x) { return x == n; });
    }

    void addSuccessor(NodeT *succ) {
        assert(succ && "Passed nullptr as the successor");
        _successors.push_back(succ);
        succ->_predecessors.push_back(static_cast<NodeT *>(this));
    }

    // return const only, so that we cannot change them
    // other way then addSuccessor()
    const NodesVec &successors() const { return _successors; }
    const NodesVec &predecessors() const { return _predecessors; }
    const NodesVec &getOperands() const { return operands; }
    const NodesVec &getUsers() const { return users; }

    void replaceSingleSuccessor(NodeT *succ) {
        assert(succ && "Passed nullptr as the successor");
        removeSingleSuccessor();
        addSuccessor(succ);
    }

    void removeSingleSuccessor() {
        assert(_successors.size() == 1);

        // we need to remove this node from
        // successor's predecessors
        _removeThisFromSuccessorsPredecessors(_successors[0]);

        // remove the successor
        _successors.clear();
    }

    // get the successor when we know there's only one of them
    NodeT *getSingleSuccessor() const {
        assert(_successors.size() == 1);
        return _successors.front();
    }

    // get the successor when there's only one of them,
    // otherwise get null
    NodeT *getSingleSuccessorOrNull() const {
        if (_successors.size() == 1)
            return _successors.front();

        return nullptr;
    }

    // get the predecessor when we know there's only one of them
    NodeT *getSinglePredecessor() const {
        assert(_predecessors.size() == 1);
        return _predecessors.front();
    }

    // get the predecessor when there's only one of them,
    // or get null
    NodeT *getSinglePredecessorOrNull() const {
        if (_predecessors.size() == 1)
            return _predecessors.front();

        return nullptr;
    }

    // insert this node in PointerGraph after n
    // this node must not be in any PointerGraph
    void insertAfter(NodeT *n) {
        assert(n && "Passed nullptr as the node");
        assert(predecessorsNum() == 0);
        assert(successorsNum() == 0);

        // take over successors
        _successors.swap(n->_successors);

        // make this node the successor of n
        n->addSuccessor(static_cast<NodeT *>(this));

        // replace the reference to n in successors
        for (NodeT *succ : _successors) {
            for (unsigned i = 0; i < succ->predecessorsNum(); ++i) {
                if (succ->_predecessors[i] == n)
                    succ->_predecessors[i] = static_cast<NodeT *>(this);
            }
        }
    }

    // insert this node in PointerGraph before n
    // this node must not be in any PointerGraph
    void insertBefore(NodeT *n) {
        assert(n && "Passed nullptr as the node");
        assert(predecessorsNum() == 0);
        assert(successorsNum() == 0);

        // take over predecessors
        _predecessors.swap(n->_predecessors);

        // 'n' is a successors of this node
        addSuccessor(n);

        // replace the reference to n in predecessors
        for (NodeT *pred : _predecessors) {
            for (unsigned i = 0; i < pred->successorsNum(); ++i) {
                if (pred->_successors[i] == n)
                    pred->_successors[i] = static_cast<NodeT *>(this);
            }
        }
    }

    // insert a sequence before this node in PointerGraph
    void insertSequenceBefore(std::pair<NodeT *, NodeT *> &seq) {
        assert(seq.first && seq.second && "Passed nullptr in the sequence");
        // the sequence must not be inserted in any PointerGraph
        assert(seq.first->predecessorsNum() == 0);
        assert(seq.second->successorsNum() == 0);

        // first node of the sequence takes over predecessors
        // this also clears 'this->predecessors' since seq.first
        // has no predecessors
        _predecessors.swap(seq.first->_predecessors);

        // replace the reference to 'this' in predecessors
        for (NodeT *pred : seq.first->_predecessors) {
            for (unsigned i = 0; i < pred->successorsNum(); ++i) {
                if (pred->_successors[i] == this)
                    pred->_successors[i] = seq.first;
            }
        }

        // this node is successors of the last node in sequence
        seq.second->addSuccessor(this);
    }

    void isolate() {
        // Remove this node from successors of the predecessors
        for (NodeT *pred : _predecessors) {
            std::vector<NodeT *> new_succs;
            new_succs.reserve(pred->_successors.size());

            for (NodeT *n : pred->_successors) {
                if (n != this)
                    new_succs.push_back(n);
            }

            new_succs.swap(pred->_successors);
        }

        // remove this nodes from successors' predecessors
        for (NodeT *succ : _successors) {
            std::vector<NodeT *> new_preds;
            new_preds.reserve(succ->_predecessors.size());

            for (NodeT *n : succ->_predecessors) {
                if (n != this)
                    new_preds.push_back(n);
            }

            new_preds.swap(succ->_predecessors);
        }

        // Take every predecessor and connect it to every successor.
        for (NodeT *pred : _predecessors) {
            for (NodeT *succ : _successors) {
                assert(succ != this && "Self-loop");
                pred->addSuccessor(succ);
            }
        }

        _successors.clear();
        _predecessors.clear();
    }

    void replaceAllUsesWith(NodeT *nd, bool removeDupl = true) {
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

    size_t predecessorsNum() const { return _predecessors.size(); }

    size_t successorsNum() const { return _successors.size(); }

#ifndef NDEBUG
    virtual void dump() const { std::cout << "SubgraphNode <" << getID(); }

    virtual void print() const {
        dump();
        std::cout << "\n";
    }

    virtual void dumpv() const { print(); }
#endif

  private:
    void _removeThisFromSuccessorsPredecessors(NodeT *succ) {
        std::vector<NodeT *> tmp;
        tmp.reserve(succ->predecessorsNum());
        for (NodeT *p : succ->_predecessors) {
            if (p != this)
                tmp.push_back(p);
        }

        succ->_predecessors.swap(tmp);
    }

    bool removeDuplicitOperands() {
        std::set<NodeT *> ops;
        bool duplicated = false;
        for (auto *op : getOperands()) {
            if (!ops.insert(op).second)
                duplicated = true;
        }

        if (duplicated) {
            operands.clear();
            operands.reserve(ops.size());
            // just push the new operads,
            // the users should not change in this case
            // (as we just remove the duplicated ones)
            for (auto *op : ops)
                operands.push_back(op);
        }

        return duplicated;
    }

    void addUser(NodeT *nd) {
        // do not add duplicate users
        for (auto *u : users)
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

} // namespace dg

#endif
