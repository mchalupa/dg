#ifndef _DG_PSS_H_
#define _DG_PSS_H_

#include <cassert>
#include <vector>
#include <cstdarg>
#include <cstring>

#include "Pointer.h"
#include "ADT/Queue.h"
#include "DFS.h"

namespace dg {
namespace analysis {

namespace pss {
enum PSSNodeType {
        // these are nodes that just represent memory allocation sites
        ALLOC = 1,
        DYN_ALLOC,
        LOAD,
        STORE,
        GEP,
        PHI,
        CAST,
        // support for interprocedural analysis
        CALL,
        // node that has only one points-to relation
        // that never changes
        CONSTANT,
        RETURN,
        // special nodes
        NULLPTR,
        UNKNOWN_MEMLOC
};
}

using pss::PSSNodeType;
using pss::ALLOC;
using pss::DYN_ALLOC;
using pss::LOAD;
using pss::STORE;
using pss::GEP;
using pss::PHI;
using pss::CALL;
using pss::RETURN;
using pss::CONSTANT;
using pss::CAST;

class PSSNode
{
    // operands cache
    // FIXME: maybe we could use SmallPtrVector
    // or something like that
    std::vector<PSSNode *> operands;
    std::vector<PSSNode *> successors;
    std::vector<PSSNode *> predecessors;

    PSSNodeType type;
    Offset offset; // for the case this node is GEP

    /// some additional information
    // was memory zeroed at initialization or right after allocating?
    bool zeroInitialized;
    // is memory allocated on heap?
    bool is_heap;
    // size of the memory
    size_t size;

    // for debugging
    const char *name;

    unsigned int dfsid;
    // data that can an analysis store in node
    // for its own usage
    void *data;

public:
    PSSNode(PSSNodeType t, ...)
    : type(t), offset(0), zeroInitialized(false), is_heap(false),
      size(0), name(nullptr), dfsid(0), data(nullptr)
    {
        // assing operands
        va_list args;
        va_start(args, t);

        switch(type) {
            case ALLOC:
            case DYN_ALLOC:
                // no operands
                break;
            case LOAD:
                operands.push_back(va_arg(args, PSSNode *));
                break;
            case STORE:
                operands.push_back(va_arg(args, PSSNode *));
                operands.push_back(va_arg(args, PSSNode *));
                break;
            case GEP:
                operands.push_back(va_arg(args, PSSNode *));
                offset = va_arg(args, Offset);
                break;
            case CAST:
                operands.push_back(va_arg(args, PSSNode *));
                break;
            case CONSTANT:
                pointsTo.insert(va_arg(args, Pointer));
                break;
            case pss::NULLPTR:
                // NULLPTR just points to itself
                pointsTo.insert(Pointer(this, 0));
                break;
            case pss::UNKNOWN_MEMLOC:
                // UNKNOWN_MEMLOC points to itself
                pointsTo.insert(Pointer(this, UNKNOWN_OFFSET));
                break;
            case PHI:
            case CALL:
            case RETURN:
                assert(0 && "Not implemented yet");
                break;
            default:
                assert(0 && "Unknown type");
        }

        va_end(args);
    }

    virtual ~PSSNode() { delete name; }

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

    PSSNodeType getType() const { return type; }
    const char *getName() const { return name; }
    void setName(const char *n) { delete name; name = strdup(n); }

    PSSNode *getOperand(int idx) const
    {
        assert(idx >= 0 && (size_t) idx < operands.size()
               && "Operand index out of range");

        return operands[idx];
    }

    void setZeroInitialized() { zeroInitialized = true; }
    bool isZeroInitialized() const { return zeroInitialized; }

    void setIsHeap() { is_heap = true; }
    bool isHeap() const { return is_heap; }

    void setSize(size_t s) { size = s; }
    size_t getSize() const { return size; }

    bool isNull() const { return type == pss::NULLPTR; }
    bool isUnknownMemory() const { return type == pss::UNKNOWN_MEMLOC; }

    void addSuccessor(PSSNode *succ)
    {
        successors.push_back(succ);
        succ->predecessors.push_back(this);
    }

    // return const only, so that we cannot change them
    // other way then addSuccessor()
    const std::vector<PSSNode *>& getSuccessors() const { return successors; }
    const std::vector<PSSNode *>& getPredecessors() const { return predecessors; }

    size_t predecessorsNum() const { return predecessors.size(); }
    size_t successorsNum() const { return successors.size(); }

    // make this public, that's basically the only
    // reason the PSS node exists, so don't hide it
    PointsToSetT pointsTo;

    // convenient helper
    bool addPointsTo(PSSNode *n, Offset o)
    {
        return pointsTo.insert(Pointer(n, o)).second;
    }

    bool addPointsTo(const Pointer& ptr)
    {
        return pointsTo.insert(ptr).second;
    }

    bool doesPointsTo(const Pointer& p)
    {
        return pointsTo.count(p) == 1;
    }

    bool doesPointsTo(PSSNode *n, Offset o = 0)
    {
        return doesPointsTo(Pointer(n, o));
    }

    friend class PSS;
};

class PSS
{
    PSSNode *root;
    unsigned int dfsnum;

protected:
    ADT::QueueFIFO<PSSNode *> queue;

public:
    bool processNode(PSSNode *);
    PSS(PSSNode *root) : root(root), dfsnum(0)
    {
        assert(root && "Cannot create PSS with null root");
    }

    // takes a PSSNode 'where' and 'what' and reference to vector and fill into the vector
    // the objects that are relevant for the PSSNode 'what' (valid memory states
    // for of this PSSNode) on place 'where' in PSS
    virtual void getMemoryObjects(PSSNode *where, PSSNode *what,
                                  std::vector<MemoryObject *>& objects) = 0;

    /*
    virtual bool addEdge(MemoryObject *from, MemoryObject *to,
                         Offset off1 = 0, Offset off2 = 0)
    {
        return false;
    }
    */

    // n is node that changed something and queues new nodes for
    // processing
    void enqueueDFS(PSSNode *n)
    {
        // default behaviour is to enqueue all pending nodes
        ++dfsnum;

        ADT::QueueLIFO<PSSNode *> lifo;
        for (PSSNode *succ : n->successors) {
            succ->dfsid = dfsnum;
            lifo.push(succ);
        }

        while (!lifo.empty()) {
            PSSNode *cur = lifo.pop();
            queue.push(cur);

            for (PSSNode *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    lifo.push(succ);
                }
            }
        }
    }

    void getNodes(std::set<PSSNode *>& cont)
    {
        assert(root && "Do not have root");

        ++dfsnum;

        ADT::QueueLIFO<PSSNode *> lifo;
        lifo.push(root);
        root->dfsid = dfsnum;

        while (!lifo.empty()) {
            PSSNode *cur = lifo.pop();
            cont.insert(cur);

            for (PSSNode *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    lifo.push(succ);
                }
            }
        }
    }

    virtual void enqueue(PSSNode *n)
    {
        // default behaviour is to queue all reachable nodes
        enqueueDFS(n);
    }

    /* hooks for analysis - optional */
    virtual void beforeProcessed(PSSNode *n)
    {
        (void) n;
    }


    virtual void afterProcessed(PSSNode *n)
    {
        (void) n;
    }

    PSSNode *getRoot() const { return root; }
    size_t pendingInQueue() const { return queue.size(); }

    void run()
    {
        assert(root && "Do not have root");
        // initialize the queue
        // FIXME let user do that
        queue.push(root);
        enqueueDFS(root);

        while (!queue.empty()) {
            PSSNode *cur = queue.pop();
            beforeProcessed(cur);

            if (processNode(cur))
                enqueue(cur);

            afterProcessed(cur);
        }
    }
};

extern PSSNode *NULLPTR;
extern PSSNode *UNKNOWN_MEMORY;

} // namespace analysis
} // namespace dg

#endif
