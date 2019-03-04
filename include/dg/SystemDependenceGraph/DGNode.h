#ifndef DG_DG_NODE_H_
#define DG_DG_NODE_H_

#include <cassert>

#ifndef NDEBUG
#include <iostream>
#endif // not NDEBUG

namespace dg {
namespace sdg {

enum class DGNodeType {
        // Invalid node
        INVALID=0
        // Ordinary instruction
        INSTRUCTION = 1,
        ARGUMENT,
        CALL,
};

inline const char *DGNodeTypeToCString(enum DGNodeType type)
{
#define ELEM(t) case t: do {return (#t); }while(0); break;
    switch(type) {
        ELEM(DGNodeType::INVALID)
        ELEM(DGNodeType::INSTRUCTION)
        ELEM(DGNodeType::ARGUMENT)
        ELEM(DGNodeType::CALL)
       default:
            assert(false && "unknown PointerSubgraph type");
            return "Unknown type";
    };
#undef ELEM
}

class PointerSubgraph;

class DGNode {
    DGNodeType _type;
    unsigned _id{0};

protected:
    Node(unsigned id, DGNodeType t) : id(_id), type(t) {}

public:
    virtual ~Node() = default;

    DGNodeType getType() const { return type; }

#ifndef NDEBUG
    virtual void dump() const {
        std::cout << "<"<< getID() << "> " << DGNodeTypeToCString(getType());
    }

    // verbose dump
    void dumpv() const override {
        dump();
        std::cout << "\n";
    }
#endif // not NDEBUG
};

// check type of node
template <DGNodeType T> bool isa(DGNode *n) {
    return n->getType() == T;
}

class DependenceGraph;

/// ----------------------------------------------------------------------
// Instruction
/// ----------------------------------------------------------------------
class DGNodeInstruction : public DGNode {
public:
    DGNodeInstruction(unsigned id) : DGNode(id, DGNodeType::INSTRUCTION) {}

    static DGNodeInstruction *get(DGNode *n) {
        return isa<DGNodeType::INSTRUCTION>(n) ?
            static_cast<DGNodeInstruction *>(n) : nullptr;
    }
};

/// ----------------------------------------------------------------------
// Call
/// ----------------------------------------------------------------------
class DGNodeCall : public Node {
    std::set<DependenceGraph *> _callees;

public:
    DGNodeCall(unsigned id) : DGNode(id, DGNodeType::CALL) {}

    static DGNodeCall *get(DGNode *n) {
        return isa<DGNodeType::CALL>(n) ?
            static_cast<DGNodeCall *>(n) : nullptr;
    }

    const std::set<DependenceGraph *>& getCallees() const { return _callees; }
    bool addCalee(DependenceGraph *g) { _callees.insert(g).second; }
};

/// ----------------------------------------------------------------------
// Argument
/// ----------------------------------------------------------------------
class DGNodeArgument : public DGNode {
public:
    DGNodeArgument(unsigned id) : DGNode(id, DGNodeType::ARGUMENT) {}

    static DGNodeArgument *get(DGNode *n) {
        return isa<DGNodeType::ARGUMENT>(n) ?
            static_cast<DGNodeArgument *>(n) : nullptr;
    }
};

} // namespace sdg
} // namespace dg

#endif
