#ifndef _DG_PS_NODE_H_
#define _DG_PS_NODE_H_

#include <cassert>
#include <cstdarg>
#include <string>

#ifndef NDEBUG
#include <iostream>
#endif // not NDEBUG

#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/analysis/PointsTo/PointsToSet.h"
#include "dg/analysis/SubgraphNode.h"

namespace dg {
namespace analysis {
namespace pta {

enum class PSNodeType {
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
        // node for convenient PointerSubgraph generation. For example as an
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

inline const char *PSNodeTypeToCString(enum PSNodeType type)
{
#define ELEM(t) case t: do {return (#t); }while(0); break;
    switch(type) {
        ELEM(PSNodeType::ALLOC)
        ELEM(PSNodeType::DYN_ALLOC)
        ELEM(PSNodeType::LOAD)
        ELEM(PSNodeType::STORE)
        ELEM(PSNodeType::GEP)
        ELEM(PSNodeType::PHI)
        ELEM(PSNodeType::CAST)
        ELEM(PSNodeType::FUNCTION)
        ELEM(PSNodeType::CALL)
        ELEM(PSNodeType::CALL_FUNCPTR)
        ELEM(PSNodeType::CALL_RETURN)
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
            assert(0 && "unknown PointerSubgraph type");
            return "Unknown type";
    };
#undef ELEM
}

class PointerSubgraph;

class PSNode : public SubgraphNode<PSNode>
{
    PSNodeType type;

    // in some cases some nodes are kind of paired - like formal and actual
    // parameters or call and return node. Here the analasis can store
    // such a node - if it needs for generating the PointerSubgraph
    // - it is not used anyhow by the base analysis itself
    // XXX: maybe we cold store this somewhere in a map instead of in every
    // node (if the map is sparse, it would be much more memory efficient)
    PSNode *pairedNode = nullptr;

    // in some cases we need to know from which function the node is
    // so we need to remember the entry node 
    PSNode *parent = nullptr;

    unsigned int dfsid = 0;

protected:
    ///
    // Construct a PSNode
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
    //               Offset::UNKNOWN constant)
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
    //               in most cases (just 'inline' the subprocedure into the PointerSubgraph
    //               when building it)
    // CALL_FUNCPTR: call via function pointer. The argument is the node that
    //               bears the pointers.
    //               FIXME: use more nodes (null-terminated list of pointer nodes)
    // CALL_RETURN:  site where given call returns. Bears the pointers
    //               returned from the subprocedure. Works like PHI
    // RETURN:       represents returning value from a subprocedure,
    //               works as a PHI node - it gathers pointers returned from
    //               the subprocedure
    // INVALIDATE_LOCALS:
    //               invalidates memory after returning from a function
    // FREE:         invalidates memory after calling free function on a pointer

    PSNode(unsigned id, PSNodeType t)
    : SubgraphNode<PSNode>(id), type(t) {
        switch(type) {
            case PSNodeType::ALLOC:
            case PSNodeType::DYN_ALLOC:
            case PSNodeType::FUNCTION:
                // these always points-to itself
                // (they points to the node where the memory was allocated)
                addPointsTo(this, 0);
                break;
            case PSNodeType::CALL:
                // the call without arguments
                break;
            default:
                break;
        }
    }

    // ctor for memcpy
    PSNode(unsigned id, PSNodeType t, PSNode *op1, PSNode *op2)
    : PSNode(id, t)
    {
        assert(t == PSNodeType::MEMCPY);
        addOperand(op1);
        addOperand(op2);
    }

    // ctor for gep
    PSNode(unsigned id, PSNodeType t, PSNode *op1)
    : PSNode(id, t)
    {
        assert(t == PSNodeType::GEP);
        addOperand(op1);
    }

    // ctor of constant
    PSNode(unsigned id, PSNodeType t, PSNode *op, Offset offset)
    : PSNode(id, t)
    {
        assert(t == PSNodeType::CONSTANT);
        // this is to correctly track def-use chains
        addOperand(op);
        pointsTo.add(Pointer(op, offset));
    }

    PSNode(unsigned id, PSNodeType t, va_list args)
    : PSNode(id, t) {
        PSNode *op;

        switch(type) {
            case PSNodeType::NOOP:
            case PSNodeType::ENTRY:
            case PSNodeType::FUNCTION:
                // no operands (and FUNCTION has been set up
                // in the super ctor)
                break;
            case PSNodeType::CAST:
            case PSNodeType::LOAD:
            case PSNodeType::CALL_FUNCPTR:
            case PSNodeType::INVALIDATE_OBJECT:
            case PSNodeType::INVALIDATE_LOCALS:
            case PSNodeType::FREE:
                addOperand(va_arg(args, PSNode *));
                break;
            case PSNodeType::STORE:
                addOperand(va_arg(args, PSNode *));
                addOperand(va_arg(args, PSNode *));
                break;
            case PSNodeType::CALL_RETURN:
            case PSNodeType::PHI:
            case PSNodeType::RETURN:
            case PSNodeType::CALL:
                op = va_arg(args, PSNode *);
                // the operands are null terminated
                while (op) {
                    addOperand(op);
                    op = va_arg(args, PSNode *);
                }
                break;
            default:
                assert(0 && "Unknown type");
        }
    }

public:

    PSNode(PSNodeType t)
    : PSNode(0, t)
    {
        switch(t) {
            case PSNodeType::INVALIDATED:
                break;
            case PSNodeType::NULL_ADDR:
                pointsTo.add(Pointer(this, 0));
                break;
            case PSNodeType::UNKNOWN_MEM:
                // UNKNOWN_MEMLOC points to itself
                pointsTo.add(Pointer(this, Offset::UNKNOWN));
                break;
            default:
                // this constructor is for the above mentioned types only
                assert(0 && "Invalid type");
                abort();
        }
    }

    virtual ~PSNode() = default;

    PSNodeType getType() const { return type; }

    void setParent(PSNode *p) { parent = p; }
    PSNode *getParent() { return parent; }
    const PSNode *getParent() const { return parent; }

    PSNode *getPairedNode() const { return pairedNode; }
    void setPairedNode(PSNode *n) { pairedNode = n; }

    bool isNull() const { return type == PSNodeType::NULL_ADDR; }
    bool isUnknownMemory() const { return type == PSNodeType::UNKNOWN_MEM; }
    bool isInvalidated() const { return type == PSNodeType::INVALIDATED; }

    // make this public, that's basically the only
    // reason the PointerSubgraph node exists, so don't hide it
    PointsToSetT pointsTo;

    // convenient helper
    bool addPointsTo(PSNode *n, Offset o)
    {
        return pointsTo.add(Pointer(n, o));
    }

    bool addPointsTo(const Pointer& ptr)
    {
        return addPointsTo(ptr.target, ptr.offset);
    }

    bool addPointsTo(const PointsToSetT& ptrs)
    {
        return pointsTo.add(ptrs);
    }

    bool doesPointsTo(const Pointer& p)
    {
        return pointsTo.count(p) == 1;
    }

    bool doesPointsTo(PSNode *n, Offset o = 0)
    {
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
        std::cout << "<"<< getID() << "> " << PSNodeTypeToCString(getType());
    }

    // verbose dump
    void dumpv() const override {
        dump();
        std::cout << "(";
        int n = 0;
        for (const auto op : getOperands()) {
            if (++n > 1)
                std::cout << ", ";
            op->dump();
        }
        std::cout << ")";

        for (const auto& ptr : pointsTo) {
            std::cout << "\n  -> ";
            ptr.dump();
        }
        std::cout << "\n";
    }
#endif // not NDEBUG

    // FIXME: maybe get rid of these friendships?
    friend class PointerAnalysis;
    friend class PointerSubgraph;

    friend void getNodes(std::set<PSNode *>& cont, PSNode *n, PSNode* exit, unsigned int dfsnum);
};


// check type of node
template <PSNodeType T> bool isa(PSNode *n) {
    return n->getType() == T;
}

class PSNodeAlloc : public PSNode {
    // was memory zeroed at initialization or right after allocating?
    bool zeroInitialized = false;
    // is memory allocated on heap?
    bool is_heap = false;
    // is it a global value?
    bool is_global = false;

public:
    PSNodeAlloc(unsigned id, PSNodeType t)
    :PSNode(id, t)
    {
        assert(t == PSNodeType::ALLOC || t == PSNodeType::DYN_ALLOC);
    }

    static PSNodeAlloc *get(PSNode *n) {
        return isa<PSNodeType::ALLOC>(n) || isa<PSNodeType::DYN_ALLOC>(n) ?
            static_cast<PSNodeAlloc *>(n) : nullptr;
    }

    void setZeroInitialized() { zeroInitialized = true; }
    bool isZeroInitialized() const { return zeroInitialized; }

    void setIsHeap() { is_heap = true; }
    bool isHeap() const { return is_heap; }

    void setIsGlobal() { is_global = true; }
    bool isGlobal() { return is_global; }
};

class PSNodeMemcpy : public PSNode {
    Offset len;

public:
    PSNodeMemcpy(unsigned id, PSNode *src, PSNode *dest, Offset len)
    :PSNode(id, PSNodeType::MEMCPY, src, dest), len(len) {}

    static PSNodeMemcpy *get(PSNode *n) {
        return isa<PSNodeType::MEMCPY>(n) ? static_cast<PSNodeMemcpy *>(n) : nullptr;
    }

    PSNode *getSource() const { return getOperand(0); }
    PSNode *getDestination() const { return getOperand(1); }
    Offset getLength() const { return len; }
};

class PSNodeGep : public PSNode {
    Offset offset;

public:
    PSNodeGep(unsigned id, PSNode *src, Offset o)
    :PSNode(id, PSNodeType::GEP, src), offset(o) {}

    static PSNodeGep *get(PSNode *n) {
        return isa<PSNodeType::GEP>(n) ? static_cast<PSNodeGep *>(n) : nullptr;
    }

    PSNode *getSource() const { return getOperand(0); }

    void setOffset(uint64_t o) { offset = o; }
    Offset getOffset() const { return offset; }
};

class PSNodeEntry : public PSNode {
    std::string functionName;

public:
    PSNodeEntry(unsigned id, const std::string& name = "not-known")
    :PSNode(id, PSNodeType::ENTRY), functionName(name) {}

    static PSNodeEntry *get(PSNode *n) {
        return isa<PSNodeType::ENTRY>(n) ?
            static_cast<PSNodeEntry *>(n) : nullptr;
    }

    void setFunctionName(const std::string& name) { functionName = name; }
    const std::string& getFunctionName() const { return functionName; }
};

class PSNodeCall : public PSNode {
    std::vector<PointerSubgraph *> callees;

public:
    PSNodeCall(unsigned id)
    :PSNode(id, PSNodeType::CALL) {}

    static PSNodeCall *get(PSNode *n) {
        return isa<PSNodeType::CALL>(n) ?
            static_cast<PSNodeCall *>(n) : nullptr;
    }

    const std::vector<PointerSubgraph *>& getCallees() const { return callees; }

    bool addCalee(PointerSubgraph *ps) {
        // we suppose there are just few callees,
        // so this should be faster than std::set
        for (PointerSubgraph *p : callees) {
            if (p == ps)
                return false;
        }

        callees.push_back(ps);
        return true;
    }
};

class PSNodeRet : public PSNode {
    // this node returns control to...
    std::vector<PSNode *> returns;

public:
    PSNodeRet(unsigned id)
    :PSNode(id, PSNodeType::RETURN) {}

    static PSNodeRet *get(PSNode *n) {
        return isa<PSNodeType::RETURN>(n) ?
            static_cast<PSNodeRet *>(n) : nullptr;
    }

    const std::vector<PSNode*>& getReturnSites() const { return returns; }

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
};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif
