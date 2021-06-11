#ifndef DG_PS_NODE_H_
#define DG_PS_NODE_H_

#include <cassert>
#include <cstdarg>
#include <iostream>
#include <string>
#include <utility>

#ifndef NDEBUG
#include <iostream>
#endif // not NDEBUG

#include "dg/PointerAnalysis/Pointer.h"
#include "dg/PointerAnalysis/PointsToSet.h"
#include "dg/SubgraphNode.h"

namespace dg {
namespace pta {

enum class PSNodeType {
    // these are nodes that just represent memory allocation sites
    ALLOC = 1,
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
    // nodes which should represent creating
    // and joining of threads
    FORK,
    JOIN,
    // node that invalidates allocated memory
    // after returning from a function
    INVALIDATE_LOCALS,
    // node that invalidates memory after calling free
    // on a pointer
    FREE,
    // node that invalidates allocated memory
    // after llvm.lifetime.end call
    INVALIDATE_OBJECT,
    // node that has only one points-to relation
    // that never changes
    CONSTANT,
    // no operation node - this nodes can be used as a branch or join
    // node for convenient PointerGraph generation. For example as an
    // unified entry to the function or unified return from the function.
    // These nodes can be optimized away later. No points-to computation
    // is performed on them
    NOOP,
    // copy whole block of memory
    MEMCPY,
    // special nodes
    NULL_ADDR,
    UNKNOWN_MEM,
    // tags memory as invalidated
    INVALIDATED
};

inline const char *PSNodeTypeToCString(enum PSNodeType type) {
#define ELEM(t)                                                                \
    case t:                                                                    \
        do {                                                                   \
            return (#t);                                                       \
        } while (0);                                                           \
        break;
    switch (type) {
        ELEM(PSNodeType::ALLOC)
        ELEM(PSNodeType::LOAD)
        ELEM(PSNodeType::STORE)
        ELEM(PSNodeType::GEP)
        ELEM(PSNodeType::PHI)
        ELEM(PSNodeType::CAST)
        ELEM(PSNodeType::FUNCTION)
        ELEM(PSNodeType::CALL)
        ELEM(PSNodeType::CALL_FUNCPTR)
        ELEM(PSNodeType::CALL_RETURN)
        ELEM(PSNodeType::FORK)
        ELEM(PSNodeType::JOIN)
        ELEM(PSNodeType::ENTRY)
        ELEM(PSNodeType::RETURN)
        ELEM(PSNodeType::CONSTANT)
        ELEM(PSNodeType::NOOP)
        ELEM(PSNodeType::MEMCPY)
        ELEM(PSNodeType::NULL_ADDR)
        ELEM(PSNodeType::UNKNOWN_MEM)
        ELEM(PSNodeType::FREE)
        ELEM(PSNodeType::INVALIDATE_OBJECT)
        ELEM(PSNodeType::INVALIDATE_LOCALS)
        ELEM(PSNodeType::INVALIDATED)
    default:
        assert(0 && "unknown PointerGraph type");
        return "Unknown type";
    };
#undef ELEM
}

class PointerGraph;
class PointerSubgraph;

class PSNode : public SubgraphNode<PSNode> {
  public:
    using IDType = SubgraphNode<PSNode>::IDType;

  private:
    PSNodeType type;

    // in some cases some nodes are kind of paired - like formal and actual
    // parameters or call and return node. Here the analasis can store
    // such a node - if it needs for generating the PointerGraph
    // - it is not used anyhow by the base analysis itself
    // XXX: maybe we cold store this somewhere in a map instead of in every
    // node (if the map is sparse, it would be much more memory efficient)
    PSNode *pairedNode = nullptr;

    // in some cases we need to know from which function the node is
    PointerSubgraph *_parent = nullptr;

    unsigned int dfsid = 0;

  public:
    ///
    // Construct a PSNode
    // \param t     type of the node
    // Different types take different arguments:
    //
    // ALLOC:        no argument
    // FUNCTION:     no argument
    // NOOP:         no argument
    // ENTRY:        no argument
    // LOAD:         one argument representing pointer to location from where
    //               we're loading the value (another pointer in this case)
    // STORE:        first argument is the value (the pointer to be stored)
    //               in memory pointed by the second argument
    // GEP:          get pointer to memory on given offset (get element pointer)
    //               first argument is pointer to the memory, second is the
    //               offset (as Offset class instance, unknown offset is
    //               represented by Offset::UNKNOWN constant)
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
    //               XXX: get rid of the arguments here?
    //               arguments are null-terminated list of nodes that can user
    //               use arbitrarily - they are not used by the analysis itself.
    //               The arguments can be used e. g. when mapping call arguments
    //               back to original CFG. Actually, the CALL node is not needed
    //               in most cases (just 'inline' the subprocedure into the
    //               PointerGraph when building it)
    // CALL_FUNCPTR: call via function pointer. The argument is the node that
    //               bears the pointers.
    // CALL_RETURN:  site where given call returns. Bears the pointers
    //               returned from the subprocedure. Works like PHI
    // RETURN:       represents returning value from a subprocedure,
    //               works as a PHI node - it gathers pointers returned from
    //               the subprocedure
    // INVALIDATE_LOCALS:
    //               invalidates memory after returning from a function
    // FREE:         invalidates memory after calling free function on a pointer

    PSNode(IDType id, PSNodeType t) : SubgraphNode<PSNode>(id), type(t) {
        switch (type) {
        case PSNodeType::ALLOC:
        case PSNodeType::FUNCTION:
            // these always points-to itself
            // (they points to the node where the memory was allocated)
            addPointsTo(this, 0);
            break;
        default:
            break;
        }
    }

    // Unfortunately, constructors cannot use enums in templates
    template <typename... Args>
    PSNode(IDType id, PSNodeType type, Args &&...args) : PSNode(id, type) {
        addOperand(std::forward<Args>(args)...);
    }

    virtual ~PSNode() = default;

    PSNodeType getType() const { return type; }

    // an auxiliary method to determine whether a node is a call
    bool isCall() const {
        return type == PSNodeType::CALL || type == PSNodeType::CALL_FUNCPTR;
    }

    void setParent(PointerSubgraph *p) { _parent = p; }
    PointerSubgraph *getParent() { return _parent; }
    const PointerSubgraph *getParent() const { return _parent; }

    PSNode *getPairedNode() const { return pairedNode; }
    void setPairedNode(PSNode *n) { pairedNode = n; }

    bool isNull() const { return type == PSNodeType::NULL_ADDR; }
    bool isUnknownMemory() const { return type == PSNodeType::UNKNOWN_MEM; }
    bool isInvalidated() const { return type == PSNodeType::INVALIDATED; }

    // make this public, that's basically the only
    // reason the PointerGraph node exists, so don't hide it
    PointsToSetT pointsTo;

    // convenient helper
    bool addPointsTo(PSNode *n, Offset o) {
        return pointsTo.add(Pointer(n, o));
    }
    bool addPointsTo(const Pointer &ptr) { return pointsTo.add(ptr); }
    bool addPointsTo(const PointsToSetT &ptrs) { return pointsTo.add(ptrs); }
    bool addPointsTo(std::initializer_list<Pointer> ptrs) {
        return pointsTo.add(ptrs);
    }

    bool doesPointsTo(const Pointer &p) const { return pointsTo.count(p) == 1; }

    bool doesPointsTo(PSNode *n, Offset o = 0) const {
        return doesPointsTo(Pointer(n, o));
    }

    ///
    // Strip all casts from the node as the
    // casts do not transform the pointer in any way
    PSNode *stripCasts() {
        PSNode *node = this;
        while (node->getType() == PSNodeType::CAST)
            node = node->getOperand(0);

        return node;
    }

#ifndef NDEBUG
    void dump() const override {
        std::cout << "<" << getID() << "> " << PSNodeTypeToCString(getType());
    }

    // verbose dump
    void dumpv() const override {
        dump();
        std::cout << "(";
        int n = 0;
        for (const auto *op : getOperands()) {
            if (++n > 1)
                std::cout << ", ";
            op->dump();
        }
        std::cout << ")";

        for (const auto &ptr : pointsTo) {
            std::cout << "\n  -> ";
            ptr.dump();
        }
        std::cout << "\n";
    }
#endif // not NDEBUG

    // FIXME: maybe get rid of these friendships?
    friend class PointerAnalysis;
    friend class PointerGraph;

    friend void getNodes(std::set<PSNode *> &cont, PSNode *n, PSNode *exit,
                         unsigned int dfsnum);
};

// check type of node
template <PSNodeType T>
bool isa(const PSNode *n) {
    return n->getType() == T;
}

template <typename T>
struct PSNodeGetter {
    static T *get(PSNode *n) { return static_cast<T *>(n); }
    static const T *get(const PSNode *n) { return static_cast<const T *>(n); }
};

template <typename T>
T *_cast(PSNode *n) {
    assert(T::get(n) && "Invalid cast");
    return T::get(n);
}

class PSNodeAlloc : public PSNode {
    // was memory zeroed at initialization or right after allocating?
    bool zeroInitialized = false;
    // is memory allocated on heap?
    bool is_heap = false;
    // is it a global value?
    bool is_global = false;
    // is it a temporary value? (its address cannot be taken)
    bool is_temporary = false;

  public:
    PSNodeAlloc(IDType id, bool isTemp = false)
            : PSNode(id, PSNodeType::ALLOC), is_temporary(isTemp) {}

    template <typename T>
    static auto get(T *n) -> decltype(PSNodeGetter<PSNodeAlloc>::get(n)) {
        return isa<PSNodeType::ALLOC>(n) ? PSNodeGetter<PSNodeAlloc>::get(n)
                                         : nullptr;
    }

    static PSNodeAlloc *cast(PSNode *n) { return _cast<PSNodeAlloc>(n); }

    void setZeroInitialized() { zeroInitialized = true; }
    bool isZeroInitialized() const { return zeroInitialized; }

    void setIsHeap() { is_heap = true; }
    bool isHeap() const { return is_heap; }

    void setIsGlobal() { is_global = true; }
    bool isGlobal() const { return is_global; }

    void setIsTemporary() { is_temporary = true; }
    bool isTemporary() const { return is_temporary; }
};

#if 0
class PSNodeTemporaryAlloc : public PSNodeAlloc {
    PSNodeTemporaryAlloc(IDType id)
    : PSNodeAlloc(id, PSNodeType::ALLOC, /* isTemp */ true) {}

    static PSNodeTemporaryAlloc *get(PSNode *n) {
        if (auto alloc = PSNodeAlloc::get(n)) {
            return alloc->isTemporary() ?
                    static_cast<PSNodeTemporaryAlloc *>(n) : nullptr;
        }

        return nullptr;
    }
};
#endif

class PSNodeConstant : public PSNode {
    Offset offset;

  public:
    PSNodeConstant(IDType id, PSNode *op, Offset offset)
            : PSNode(id, PSNodeType::CONSTANT, op), offset(offset) {
        addPointsTo(op, offset);
    }

    static PSNodeConstant *get(PSNode *n) {
        return isa<PSNodeType::CONSTANT>(n) ? static_cast<PSNodeConstant *>(n)
                                            : nullptr;
    }

    static PSNodeConstant *cast(PSNode *n) { return _cast<PSNodeConstant>(n); }

    Pointer getPointer() const { return {getOperand(0), offset}; }
    Offset getOffset() const { return offset; }
    PSNode *getTarget() { return getOperand(0); }
    const PSNode *getTarget() const { return getOperand(0); }
};

class PSNodeMemcpy : public PSNode {
    Offset len;

  public:
    PSNodeMemcpy(IDType id, PSNode *src, PSNode *dest, Offset len)
            : PSNode(id, PSNodeType::MEMCPY, src, dest), len(len) {}

    static PSNodeMemcpy *get(PSNode *n) {
        return isa<PSNodeType::MEMCPY>(n) ? static_cast<PSNodeMemcpy *>(n)
                                          : nullptr;
    }

    static PSNodeMemcpy *cast(PSNode *n) { return _cast<PSNodeMemcpy>(n); }

    PSNode *getSource() const { return getOperand(0); }
    PSNode *getDestination() const { return getOperand(1); }
    Offset getLength() const { return len; }
};

class PSNodeGep : public PSNode {
    Offset offset;

  public:
    PSNodeGep(IDType id, PSNode *src, Offset o)
            : PSNode(id, PSNodeType::GEP, src), offset(o) {}

    static PSNodeGep *get(PSNode *n) {
        return isa<PSNodeType::GEP>(n) ? static_cast<PSNodeGep *>(n) : nullptr;
    }

    // get() with a check
    static PSNodeGep *cast(PSNode *n) { return _cast<PSNodeGep>(n); }

    PSNode *getSource() const { return getOperand(0); }

    void setOffset(uint64_t o) { offset = o; }
    Offset getOffset() const { return offset; }
};

class PSNodeEntry : public PSNode {
    std::string functionName;
    std::vector<PSNode *> callers;

  public:
    PSNodeEntry(IDType id, std::string name = "not-known")
            : PSNode(id, PSNodeType::ENTRY), functionName(std::move(name)) {}

    static PSNodeEntry *get(PSNode *n) {
        return isa<PSNodeType::ENTRY>(n) ? static_cast<PSNodeEntry *>(n)
                                         : nullptr;
    }
    static PSNodeEntry *cast(PSNode *n) { return _cast<PSNodeEntry>(n); }

    void setFunctionName(const std::string &name) { functionName = name; }
    const std::string &getFunctionName() const { return functionName; }

    const std::vector<PSNode *> &getCallers() const { return callers; }

    bool addCaller(PSNode *n) {
        // we suppose there are just few callees,
        // so this should be faster than std::set
        for (auto *p : callers) {
            if (p == n)
                return false;
        }

        callers.push_back(n);
        return true;
    }
};

class PSNodeCall : public PSNode {
    // what this call calls?
    std::vector<PointerSubgraph *> callees;
    // where it returns?
    PSNode *callReturn{nullptr};

  public:
    PSNodeCall(IDType id) : PSNode(id, PSNodeType::CALL) {}

    PSNodeCall(IDType id, PSNode *op)
            : PSNode(id, PSNodeType::CALL_FUNCPTR, op) {}

    static PSNodeCall *get(PSNode *n) {
        return (isa<PSNodeType::CALL>(n) || isa<PSNodeType::CALL_FUNCPTR>(n))
                       ? static_cast<PSNodeCall *>(n)
                       : nullptr;
    }
    static PSNodeCall *cast(PSNode *n) { return _cast<PSNodeCall>(n); }

    void setCallReturn(PSNode *callRet) { callReturn = callRet; }
    PSNode *getCallReturn() { return callReturn; }
    const PSNode *getCallReturn() const { return callReturn; }

    const std::vector<PointerSubgraph *> &getCallees() const { return callees; }

    bool addCallee(PointerSubgraph *ps) {
        // we suppose there are just few callees,
        // so this should be faster than std::set
        for (PointerSubgraph *p : callees) {
            if (p == ps)
                return false;
        }

        callees.push_back(ps);
        return true;
    }

#ifndef NDEBUG
    // verbose dump
    void dumpv() const override {
        PSNode::dumpv();
        if (callReturn)
            std::cout << "returns to " << callReturn->getID();
        else
            std::cout << "does not return ";

        std::cout << " calls: [";
        int n = 0;
        for (const auto *op : callees) {
            if (++n > 1)
                std::cout << ", ";
            std::cout << op;
        }
        std::cout << "]";
        std::cout << "\n";
    }
#endif // not NDEBUG
};

class PSNodeCallRet : public PSNode {
    // return nodes that go to this call-return node
    std::vector<PSNode *> returns;
    PSNode *call;

  public:
    template <typename... Args>
    PSNodeCallRet(IDType id, Args &&...args)
            : PSNode(id, PSNodeType::CALL_RETURN, std::forward<Args>(args)...) {
    }

    static PSNodeCallRet *get(PSNode *n) {
        return isa<PSNodeType::CALL_RETURN>(n) ? static_cast<PSNodeCallRet *>(n)
                                               : nullptr;
    }

    static PSNodeCallRet *cast(PSNode *n) { return _cast<PSNodeCallRet>(n); }

    void setCall(PSNode *c) { call = c; }
    PSNode *getCall() { return call; }
    const PSNode *getCall() const { return call; }

    const std::vector<PSNode *> &getReturns() const { return returns; }

    bool addReturn(PSNode *p) {
        // we suppose there are just few callees,
        // so this should be faster than std::set
        for (auto *r : returns) {
            if (p == r)
                return false;
        }

        returns.push_back(p);
        return true;
    }

#ifndef NDEBUG
    // verbose dump
    void dumpv() const override {
        PSNode::dumpv();
        std::cout << "Return-site of call " << call->getID() << " rets: [";
        int n = 0;
        for (const auto *op : returns) {
            if (++n > 1)
                std::cout << ", ";
            op->dump();
        }
        std::cout << "]";
        std::cout << "\n";
    }
#endif // not NDEBUG
};

class PSNodeRet : public PSNode {
    // this node returns control to...
    std::vector<PSNode *> returns;

  public:
    template <typename... Args>
    PSNodeRet(IDType id, Args &&...args)
            : PSNode(id, PSNodeType::RETURN, std::forward<Args>(args)...) {}

    static PSNodeRet *get(PSNode *n) {
        return isa<PSNodeType::RETURN>(n) ? static_cast<PSNodeRet *>(n)
                                          : nullptr;
    }

    const std::vector<PSNode *> &getReturnSites() const { return returns; }

    bool addReturnSite(PSNode *r) {
        // we suppose there are just few callees,
        // so this should be faster than std::set
        for (PSNode *p : returns) {
            if (p == r)
                return false;
        }

        returns.push_back(r);
        return true;
    }

#ifndef NDEBUG
    // verbose dump
    void dumpv() const override {
        PSNode::dumpv();
        std::cout << "Returns from: [";
        int n = 0;
        for (const auto *op : returns) {
            if (++n > 1)
                std::cout << ", ";
            op->dump();
        }
        std::cout << "]";
        std::cout << "\n";
    }
#endif // not NDEBUG
};

class PSNodeFork;
class PSNodeJoin;

class PSNodeFork : public PSNode {
    PSNode *callInstruction = nullptr;
    std::set<PSNodeJoin *> joins;
    std::set<PSNode *> functions_;

  public:
    PSNodeFork(IDType id, PSNode *from) : PSNode(id, PSNodeType::FORK, from) {}

    static PSNodeFork *get(PSNode *n) {
        return isa<PSNodeType::FORK>(n) ? static_cast<PSNodeFork *>(n)
                                        : nullptr;
    }
    static PSNodeFork *cast(PSNode *n) { return _cast<PSNodeFork>(n); }

    std::set<PSNodeJoin *> getJoins() const { return joins; }

    bool addFunction(PSNode *function) {
        return functions_.insert(function).second;
    }

    std::set<PSNode *> functions() const { return functions_; }

    void setCallInst(PSNode *callInst) { callInstruction = callInst; }

    PSNode *callInst() const { return callInstruction; }

    friend class PSNodeJoin;
};

class PSNodeJoin : public PSNode {
    PSNode *callInstruction = nullptr;
    std::set<PSNodeFork *> forks_;
    std::set<PSNode *> functions_;

  public:
    PSNodeJoin(IDType id) : PSNode(id, PSNodeType::JOIN) {}

    static PSNodeJoin *get(PSNode *n) {
        return isa<PSNodeType::JOIN>(n) ? static_cast<PSNodeJoin *>(n)
                                        : nullptr;
    }
    static PSNodeJoin *cast(PSNode *n) { return _cast<PSNodeJoin>(n); }

    void setCallInst(PSNode *callInst) { callInstruction = callInst; }

    PSNode *callInst() const { return callInstruction; }

    bool addFunction(PSNode *function) {
        return functions_.insert(function).second;
    }

    bool addFork(PSNodeFork *fork) {
        forks_.insert(fork);
        return fork->joins.insert(this).second;
    }

    std::set<PSNodeFork *> forks() { return forks_; }

    std::set<PSNode *> functions() const { return functions_; }

    friend class PSNodeFork;
};

template <PSNodeType Type, typename = void>
struct GetNodeType {
    using type = PSNode;
};

template <>
struct GetNodeType<PSNodeType::ALLOC> {
    using type = PSNodeAlloc;
};

template <>
struct GetNodeType<PSNodeType::CONSTANT> {
    using type = PSNodeConstant;
};

template <>
struct GetNodeType<PSNodeType::GEP> {
    using type = PSNodeGep;
};

template <>
struct GetNodeType<PSNodeType::MEMCPY> {
    using type = PSNodeMemcpy;
};

template <>
struct GetNodeType<PSNodeType::ENTRY> {
    using type = PSNodeEntry;
};

template <PSNodeType Type>
struct GetNodeType<
        Type, typename std::enable_if<Type == PSNodeType::CALL ||
                                      Type == PSNodeType::CALL_FUNCPTR>::type> {
    using type = PSNodeCall;
};

template <>
struct GetNodeType<PSNodeType::FORK> {
    using type = PSNodeFork;
};

template <>
struct GetNodeType<PSNodeType::JOIN> {
    using type = PSNodeJoin;
};

template <>
struct GetNodeType<PSNodeType::RETURN> {
    using type = PSNodeRet;
};

template <>
struct GetNodeType<PSNodeType::CALL_RETURN> {
    using type = PSNodeCallRet;
};

} // namespace pta
} // namespace dg

#endif
