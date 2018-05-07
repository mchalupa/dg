#ifndef _SUBGRAPH_NODE_H_
#define _SUBGRAPH_NODE_H_

// This file defines a basis for nodes from
// PointerSubgraph and reaching definitions subgraph.

#include <vector>

namespace dg {
namespace analysis {

template <typename NodeT>
class SubgraphNode {
    // id of the node. Every node from a graph has a unique ID;
    unsigned int id = 0;

    // data that can an analysis store in node
    // for its own needs
    void *data;

    // data that can user store in the node
    // NOTE: I considered if this way is better than
    // creating subclass of PSNode and have whatever we
    // need in the subclass. Since AFAIK we need just this one pointer
    // at this moment, I decided to do it this way since it
    // is more simple than dynamic_cast... Once we need more
    // than one pointer, we can change this design.
    void *user_data;
protected:
    // XXX: make those private?
    std::vector<NodeT *> successors;
    std::vector<NodeT *> predecessors;
    // XXX: maybe we could use SmallPtrVector or something like that
    std::vector<NodeT *> operands;

    // size of the memory
    size_t size;
public:
    // FIXME: make this private, this is just for debugging
    /// for computing SCC
    //XXX: we could do this more generic
    // the dfs order
    unsigned int dfs_id;
    unsigned int lowpt;

    // id of scc component
    unsigned int scc_id;
    // true if the node is on stack
    bool on_stack;

    SubgraphNode<NodeT>(unsigned id)
    : id(id), data(nullptr), user_data(nullptr), size(0),
      dfs_id(0), lowpt(0), scc_id(0), on_stack(false)
    {}

    unsigned int getID() const { return id; }

    void setSize(size_t s) { size = s; }
    size_t getSize() const { return size; }

    unsigned getSCCId() const
    {
        return scc_id;
    }

    // getters & setters for analysis's data in the node
    template <typename T>
    T* getData() { return static_cast<T *>(data); }
    template <typename T>
    const T* getData() const { return static_cast<T *>(data); }

    template <typename T>
    void *setData(T *newdata)
    {
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
    void *setUserData(T *newdata)
    {
        void *old = user_data;
        user_data = static_cast<void *>(newdata);
        return old;
    }

    NodeT *getOperand(int idx) const
    {
        assert(idx >= 0 && static_cast<size_t>(idx) < operands.size()
               && "Operand index out of range");

        return operands[idx];
    }

    size_t getOperandsNum() const
    {
        return operands.size();
    }

    size_t addOperand(NodeT *n)
    {
        assert(n && "Passed nullptr as the operand");
        operands.push_back(n);
        return operands.size();
    }

    void addSuccessor(NodeT *succ)
    {
        assert(succ && "Passed nullptr as the successor");
        successors.push_back(succ);
        succ->predecessors.push_back(static_cast<NodeT *>(this));
    }

    // return const only, so that we cannot change them
    // other way then addSuccessor()
    const std::vector<NodeT *>& getSuccessors() const
    {
        return successors;
    }

    const std::vector<NodeT *>& getPredecessors() const
    {
        return predecessors;
    }

    const std::vector<NodeT *>& getOperands() const
    {
        return operands;
    }

    void replaceSingleSuccessor(NodeT *succ)
    {
        assert(succ && "Passed nullptr as the successor");
        assert(successors.size() == 1);
        NodeT *old = successors[0];

        // we need to remove this node from
        // successor's predecessors
        std::vector<NodeT *> tmp;
        tmp.reserve(old->predecessorsNum() - 1);
        for (NodeT *p : old->predecessors) {
            if (p != this)
                tmp.push_back(p);
        }

        old->predecessors.swap(tmp);

        // replace the successor
        successors.clear();
        addSuccessor(succ);
    }

    // get successor when we know there's only one of them
    NodeT *getSingleSuccessor() const
    {
        assert(successors.size() == 1);
        return successors.front();
    }

    // get predecessor when we know there's only one of them
    NodeT *getSinglePredecessor() const
    {
        assert(predecessors.size() == 1);
        return predecessors.front();
    }

    // insert this node in PointerSubgraph after n
    // this node must not be in any PointerSubgraph
    void insertAfter(NodeT *n)
    {
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
    void insertBefore(NodeT *n)
    {
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
    void insertSequenceBefore(std::pair<NodeT *, NodeT *>& seq)
    {
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

    size_t predecessorsNum() const
    {
        return predecessors.size();
    }

    size_t successorsNum() const
    {
        return successors.size();
    }
};

} // analysis
} // dg
#endif // _SUBGRAPH_NODE_H_
