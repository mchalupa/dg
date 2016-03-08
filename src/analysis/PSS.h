#ifndef _DG_PSS_H_
#define _DG_PSS_H_

#include <cassert>
#include <vector>
#include <cstdarg>
#include <cstring> // for strdup

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
        // support for calls via function pointers.
        // The FUNCTION node is the same as ALLOC
        // but having it as separate type has the nice
        // advantage of type checking
        FUNCTION,
        // support for interprocedural analysis,
        // operands are null terminated. It is a noop,
        // just for the user's convenience
        CALL,
        // call via function pointer
        CALL_FUNCPTR,
        CALL_RETURN,
        // this is the entry node of a subprocedure
        // and serves just as no op for our convenience,
        // can be optimized away later
        ENTRY,
        // this is the exit node of a subprocedure
        // that returns a value - works as phi node
        RETURN,
        // node that has only one points-to relation
        // that never changes
        CONSTANT,
        // no operation node - this nodes can be used as a branch or join
        // node for convenient PSS generation. For example as an
        // unified entry to the function or unified return from the function.
        // These nodes can be optimized away later. No points-to computation
        // is performed on them
        NOOP,
        // special nodes
        NULL_ADDR,
        UNKNOWN_MEM,
};

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
    unsigned int dfsid2;
    // data that can an analysis store in node
    // for its own needs
    void *data;

    // data that can user store in the node
    // NOTE: I considered if this way is better than
    // creating subclass of PSSNode and have whatever we
    // need in the subclass. Since AFAIK we need just this one pointer
    // at this moment, I decided to do it this way since it
    // is more simple than dynamic_cast... Once we need more
    // than one pointer, we can change this design.
    void *user_data;

public:
    ///
    // Construct a PSSNode
    // \param t     type of the node
    // Different types take different arguments:
    //
    // ALLOC:       no argument
    // DYN_ALLOC:   no argument
    // FUNCTION:    no argument
    // NOOP:        no argument
    // ENTRY:       no argument
    // LOAD:        one argument representing pointer to location from where
    //              we're loading the value (another pointer in this case)
    // STORE:       first argument is the value (the pointer to be stored)
    //              in memory pointed by the second argument
    // GEP:         get pointer to memory on given offset (get element pointer)
    //              first argument is pointer to the memory, second is the offset
    //              (as Offset class instance, unknown offset is represented by
    //              UNKNOWN_OFFSET constant)
    // CAST:        cast pointer from one type to other type (like void * to int *)
    //              the argument is just the pointer (we don't care about types
    //              atm.)
    // FUNCTION:    Object representing the function in memory - so that it
    //              can be pointed to and used as an argument to the Pointer
    // CONSTANT:    node that keeps constant points-to information
    //              the argument is the pointer it points to
    // PHI:         phi node that gathers pointers from different paths in CFG
    //              arguments are null-terminated list of the relevant nodes
    //              from predecessors
    // CALL:        represents call of subprocedure,
    //              arguments are null-terminated list of nodes that can user
    //              use arbitrarily - they are not used by the analysis itself.
    //              The arguments can be used e. g. when mapping call arguments back to
    //              original CFG. Actually, the CALL node is not needed in most
    //              cases (just 'inline' the subprocedure into the PSS when building it)
    //              The call node is needed when the function is called
    //              via function pointer though.
    // CALL_FUNCPTR: call via function pointer. Takes one argument - pointer that
    //              is used to the call
    // CALL_RETURN: site where given call returns. Bears the pointers returned from
    //              the subprocedure. Works like PHI
    // RETURN:      represents returning value from a subprocedure,
    //              works as a PHI node - it gathers pointers returned from the subprocedure
    PSSNode(PSSNodeType t, ...)
    : type(t), offset(0), zeroInitialized(false), is_heap(false),
      size(0), name(nullptr), dfsid(0), dfsid2(0), data(nullptr), user_data(nullptr)
    {
        // assing operands
        PSSNode *op;
        va_list args;
        va_start(args, t);

        switch(type) {
            case ALLOC:
            case DYN_ALLOC:
            case FUNCTION:
                // these always points-to itself
                // (they points to the node where the memory was allocated)
                addPointsTo(this, 0);
                break;
            case NOOP:
            case ENTRY:
                // no operands
                break;
            case CAST:
            case LOAD:
            case CALL_FUNCPTR:
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
            case CONSTANT:
                pointsTo.insert(va_arg(args, Pointer));
                break;
            case NULL_ADDR:
                pointsTo.insert(Pointer(this, 0));
                break;
            case pss::UNKNOWN_MEM:
                // UNKNOWN_MEMLOC points to itself
                pointsTo.insert(Pointer(this, UNKNOWN_OFFSET));
                break;
            case CALL_RETURN:
            case PHI:
            case RETURN:
            case CALL:
                op = va_arg(args, PSSNode *);
                // the operands are null terminated
                while (op) {
                    operands.push_back(op);
                    op = va_arg(args, PSSNode *);
                }
                break;
            default:
                assert(0 && "Unknown type");
        }

        va_end(args);
    }

    virtual ~PSSNode() { delete name; }

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

    PSSNodeType getType() const { return type; }
    const char *getName() const { return name; }
    void setName(const char *n) { delete name; name = strdup(n); }

    PSSNode *getOperand(int idx) const
    {
        assert(idx >= 0 && (size_t) idx < operands.size()
               && "Operand index out of range");

        return operands[idx];
    }

    size_t addOperand(PSSNode *n)
    {
        operands.push_back(n);
        return operands.size();
    }

    void setZeroInitialized() { zeroInitialized = true; }
    bool isZeroInitialized() const { return zeroInitialized; }

    void setIsHeap() { is_heap = true; }
    bool isHeap() const { return is_heap; }

    void setSize(size_t s) { size = s; }
    size_t getSize() const { return size; }

    bool isNull() const { return type == NULL_ADDR; }
    bool isUnknownMemory() const { return type == UNKNOWN_MEM; }

    void addSuccessor(PSSNode *succ)
    {
        successors.push_back(succ);
        succ->predecessors.push_back(this);
    }

    // return const only, so that we cannot change them
    // other way then addSuccessor()
    const std::vector<PSSNode *>& getSuccessors() const { return successors; }
    const std::vector<PSSNode *>& getPredecessors() const { return predecessors; }

    // get successor when we know there's only one of them
    PSSNode *getSingleSuccessor() const
    {
        assert(successors.size() == 1);
        return successors.front();
    }

    // get predecessor when we know there's only one of them
    PSSNode *getSinglePredecessor() const
    {
        assert(predecessors.size() == 1);
        return predecessors.front();
    }

    // insert this node in PSS after n
    // this node must not be in any PSS
    void insertAfter(PSSNode *n)
    {
        assert(predecessorsNum() == 0);
        assert(successorsNum() == 0);

        // take over successors
        successors.swap(n->successors);

        // make this node the successor of n
        n->addSuccessor(this);

        // replace the reference to n in successors
        for (PSSNode *succ : successors) {
            for (unsigned i = 0; i < succ->predecessorsNum(); ++i) {
                if (succ->predecessors[i] == n)
                    succ->predecessors[i] = this;
            }
        }
    }

    size_t predecessorsNum() const { return predecessors.size(); }
    size_t successorsNum() const { return successors.size(); }

    // make this public, that's basically the only
    // reason the PSS node exists, so don't hide it
    PointsToSetT pointsTo;

    // convenient helper
    bool addPointsTo(PSSNode *n, Offset o)
    {
        // do not add concrete offsets when we have the UNKNOWN_OFFSET
        // - unknown offset stands for any offset
        if (pointsTo.count(Pointer(n, UNKNOWN_OFFSET)))
            return false;

        return pointsTo.insert(Pointer(n, o)).second;
    }

    bool addPointsTo(const Pointer& ptr)
    {
        return addPointsTo(ptr.target, ptr.offset);
    }

    bool addPointsTo(const std::set<Pointer>& ptrs)
    {
        bool changed = false;
        for (const Pointer& ptr: ptrs)
            changed |= addPointsTo(ptr);

        return changed;
    }



    bool doesPointsTo(const Pointer& p)
    {
        return pointsTo.count(p) == 1;
    }

    bool doesPointsTo(PSSNode *n, Offset o = 0)
    {
        return doesPointsTo(Pointer(n, o));
    }

    bool addPointsToUnknownOffset(PSSNode *target);

    // get all nodes that has no successor (starting search from this node)
    void getLeafs(std::vector<PSSNode *>& leafs)
    {
        static unsigned dfsnum;
        ++dfsnum;

        ADT::QueueLIFO<PSSNode *> lifo;
        lifo.push(this);
        dfsid2 = dfsnum;

        while (!lifo.empty()) {
            PSSNode *cur = lifo.pop();

            if (cur->successorsNum() == 0) {
                leafs.push_back(cur);
            } else {
                for (PSSNode *succ : cur->successors) {
                    if (succ->dfsid2 != dfsnum) {
                        succ->dfsid2 = dfsnum;
                        lifo.push(succ);
                    }
                }
            }
        }
    }

    friend class PSS;
};

// special PSS nodes
extern PSSNode *NULLPTR;
extern PSSNode *UNKNOWN_MEMORY;

class PSS
{
    unsigned int dfsnum;

    // queue used to reach the fixpoint
    ADT::QueueFIFO<PSSNode *> queue;

    // root of the pointer state subgraph
    PSSNode *root;

protected:
    // protected constructor for child classes
    PSS() : dfsnum(0), root(nullptr) {}

public:
    bool processNode(PSSNode *);
    PSS(PSSNode *root) : dfsnum(0), root(nullptr)
    {
        assert(root && "Cannot create PSS with null root");

        // XXX: couldn't we do it somehow better?
        #ifdef DEBUG_ENABLED
        if (!NULLPTR->getName()) {
            NULLPTR->setName("nullptr");
            UNKNOWN_MEMORY->setName("unknown memloc");
        }
        #endif
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
    void setRoot(PSSNode *r) { root = r; }

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

    // handle error in the analysis
    // @return whether the function changed the some points-to set
    //  (e. g. added pointer to unknown memory)
    virtual bool errorEmptyPointsTo(PSSNode *from, PSSNode *to)
    {
        // let this on the user - in flow-insensitive analysis this is
        // no error, but in flow sensitive it is ...
        (void) from;
        (void) to;
        return false;
    }

    // adjust the PSS on function pointer call
    // @ where is the callsite
    // @ what is the function that is being called
    virtual bool functionPointerCall(PSSNode *where, PSSNode *what)
    {
        (void) where;
        (void) what;
        return false;
    }

private:
    bool processLoad(PSSNode *node);
};

} // namespace pss
} // namespace analysis
} // namespace dg

#endif
