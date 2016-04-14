#ifndef _DG_PSS_H_
#define _DG_PSS_H_

#include <cassert>
#include <vector>
#include <cstdarg>
#include <cstring> // for strdup

#include "Pointer.h"
#include "ADT/Queue.h"

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
        // return from the subprocedure (in caller),
        // synonym to PHI
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
        // copy whole block of memory
        MEMCPY,
        // special nodes
        NULL_ADDR,
        UNKNOWN_MEM,
};

class PSSNode
{
    // FIXME: maybe we could use SmallPtrVector or something like that
    std::vector<PSSNode *> operands;
    std::vector<PSSNode *> successors;
    std::vector<PSSNode *> predecessors;

    PSSNodeType type;
    Offset offset; // for the case this node is GEP or MEMCPY
    Offset len; // for the case this node is MEMCPY

    // in some cases some nodes are kind of paired - like formal and actual
    // parameters or call and return node. Here the analasis can store
    // such a node - if it needs for generating the PSS
    // - it is not used anyhow by the base analysis itself
    // XXX: maybe we cold store this somewhere in a map instead of in every
    // node (if the map is sparse, it would be much more memory efficient)
    PSSNode *pairedNode;

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
    // ALLOC:        no argument
    // DYN_ALLOC:    no argument
    // FUNCTION:     no argument
    // NOOP:         no argument
    // ENTRY:        no argument
    // LOAD:         one argument representing pointer to location from where
    //               we're loading the value (another pointer in this case)
    // STORE:        first argument is the value (the pointer to be stored)
    //               in memory pointed by the second argument
    // GEP:          get pointer to memory on given offset (get element pointer)
    //               first argument is pointer to the memory, second is the offset
    //               (as Offset class instance, unknown offset is represented by
    //               UNKNOWN_OFFSET constant)
    // CAST:         cast pointer from one type to other type (like void * to
    //               int *). The pointers are just copied, so we can optimize
    //               away this node later. The argument is just the pointer
    //               (we don't care about types atm.)
    // MEMCPY:       Copy whole block of memory. <from> <to> <offset> <len>
    // FUNCTION:     Object representing the function in memory - so that it
    //               can be pointed to and used as an argument to the Pointer
    // CONSTANT:     node that keeps constant points-to information
    //               the argument is the pointer it points to
    // PHI:          phi node that gathers pointers from different paths in CFG
    //               arguments are null-terminated list of the relevant nodes
    //               from predecessors
    // CALL:         represents call of subprocedure,
    //               arguments are null-terminated list of nodes that can user
    //               use arbitrarily - they are not used by the analysis itself.
    //               The arguments can be used e. g. when mapping call arguments
    //               back to original CFG. Actually, the CALL node is not needed
    //               in most cases (just 'inline' the subprocedure into the PSS
    //               when building it)
    // CALL_FUNCPTR: call via function pointer. The argument is the node that
    //               bears the pointers.
    //               FIXME: use more nodes (null-terminated list of pointer nodes)
    // CALL_RETURN:  site where given call returns. Bears the pointers
    //               returned from the subprocedure. Works like PHI
    // RETURN:       represents returning value from a subprocedure,
    //               works as a PHI node - it gathers pointers returned from
    //               the subprocedure
    PSSNode(PSSNodeType t, ...)
    : type(t), offset(0), pairedNode(nullptr), zeroInitialized(false),
      is_heap(false), size(0), name(nullptr), dfsid(0),
      data(nullptr), user_data(nullptr)
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
            case MEMCPY:
                operands.push_back(va_arg(args, PSSNode *));
                operands.push_back(va_arg(args, PSSNode *));
                offset = va_arg(args, uint64_t);
                len = va_arg(args, uint64_t);
                break;
            case GEP:
                operands.push_back(va_arg(args, PSSNode *));
                offset = va_arg(args, uint64_t);
                break;
            case CONSTANT:
                op = va_arg(args, PSSNode *);
                offset = va_arg(args, uint64_t);
                pointsTo.insert(Pointer(op, offset));
                break;
            case NULL_ADDR:
                pointsTo.insert(Pointer(this, 0));
#ifdef DEBUG_ENABLED
                setName("null");
#endif
                break;
            case pss::UNKNOWN_MEM:
                // UNKNOWN_MEMLOC points to itself
                pointsTo.insert(Pointer(this, UNKNOWN_OFFSET));
#ifdef DEBUG_ENABLED
                setName("unknown");
#endif
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

    ~PSSNode() { delete name; }

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

    PSSNode *getPairedNode() const { return pairedNode; }
    void setPairedNode(PSSNode *n) { pairedNode = n; }

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

    void replaceSingleSuccessor(PSSNode *succ)
    {
        assert(successors.size() == 1);
        PSSNode *old = successors[0];

        // replace the successor
        successors.clear();
        addSuccessor(succ);

        // we need to remove this node from
        // successor's predecessors
        std::vector<PSSNode *> tmp;
        tmp.reserve(old->predecessorsNum() - 1);
        for (PSSNode *p : old->predecessors)
            tmp.push_back(p);

        old->predecessors.swap(tmp);
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

    // insert this node in PSS before n
    // this node must not be in any PSS
    void insertBefore(PSSNode *n)
    {
        assert(predecessorsNum() == 0);
        assert(successorsNum() == 0);

        // take over predecessors
        predecessors.swap(n->predecessors);

        // 'n' is a successors of this node
        addSuccessor(n);

        // replace the reference to n in predecessors
        for (PSSNode *pred : predecessors) {
            for (unsigned i = 0; i < pred->successorsNum(); ++i) {
                if (pred->successors[i] == n)
                    pred->successors[i] = this;
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

        if (o.isUnknown())
            return addPointsToUnknownOffset(n);
        else
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

    friend class PSS;
};

// special PSS nodes
extern PSSNode *NULLPTR;
extern PSSNode *UNKNOWN_MEMORY;

class PSS
{
    unsigned int dfsnum;

    // root of the pointer state subgraph
    PSSNode *root;

protected:
    // queue used to reach the fixpoint
    ADT::QueueFIFO<PSSNode *> queue;

    // protected constructor for child classes
    PSS() : dfsnum(0), root(nullptr) {}

public:
    bool processNode(PSSNode *);
    PSS(PSSNode *r) : dfsnum(0), root(r)
    {
        assert(root && "Cannot create PSS with null root");
    }

    virtual ~PSS() {}

    // takes a PSSNode 'where' and 'what' and reference to a vector
    // and fills into the vector the objects that are relevant
    // for the PSSNode 'what' (valid memory states for of this PSSNode)
    // on location 'where' in PSS
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

    void getNodes(std::set<PSSNode *>& cont,
                  bool (*filter)(PSSNode *, void *) = nullptr,
                  void *filter_data = nullptr)
    {
        assert(root && "Do not have root");

        ++dfsnum;

        ADT::QueueFIFO<PSSNode *> fifio;
        fifio.push(root);
        root->dfsid = dfsnum;

        while (!fifio.empty()) {
            PSSNode *cur = fifio.pop();
            if (!filter || filter(cur, filter_data))
                cont.insert(cur);

            for (PSSNode *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    fifio.push(succ);
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

#ifdef DEBUG_ENABLED
        // NOTE: This works as assertion,
        // we'd like to be sure that we have reached the fixpoint,
        // so we'll do one more iteration and check it

        queue.push(root);
        enqueueDFS(root);

        bool changed = false;
        while (!queue.empty()) {
            PSSNode *cur = queue.pop();

            beforeProcessed(cur);

            changed = processNode(cur);
            assert(!changed && "BUG: Did not reach fixpoint");

            afterProcessed(cur);
        }
#endif // DEBUG_ENABLED
    }

    // generic error
    // @msg - message for the user
    // FIXME: maybe create some enum that will represent the error
    virtual bool error(PSSNode *at, const char *msg)
    {
        // let this on the user - in flow-insensitive analysis this is
        // no error, but in flow sensitive it is ...
        (void) at;
        (void) msg;
        return false;
    }

    // handle specific situation (error) in the analysis
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
    bool processMemcpy(PSSNode *node);
};

} // namespace pss
} // namespace analysis
} // namespace dg

#endif
