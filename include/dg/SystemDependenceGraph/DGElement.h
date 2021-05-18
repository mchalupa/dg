#ifndef DG_DG_ELEMENT_H_
#define DG_DG_ELEMENT_H_

#include <cassert>

#ifndef NDEBUG
// FIXME: move this to .cpp file
#include <iostream>
#endif // not NDEBUG

namespace dg {
namespace sdg {

enum class DGElementType {
    // Invalid node
    INVALID = 0,
    /// special elements
    // Pair of arguments (input & output)
    ARG_PAIR = 1,
    BBLOCK,
    // nodes
    // NOTE: here can follow only childs of DGNode class
    NODE = 3,
    ND_INSTRUCTION,
    ND_ARGUMENT,
    ND_CALL,
    ND_ARTIFICIAL
};

inline const char *DGElemTypeToCString(enum DGElementType type) {
#define ELEM(t)                                                                \
    case t:                                                                    \
        do {                                                                   \
            return (#t);                                                       \
        } while (0);                                                           \
        break;
    switch (type) {
        ELEM(DGElementType::INVALID)
        ELEM(DGElementType::ARG_PAIR)
        ELEM(DGElementType::BBLOCK)
        ELEM(DGElementType::NODE)
        ELEM(DGElementType::ND_INSTRUCTION)
        ELEM(DGElementType::ND_ARGUMENT)
        ELEM(DGElementType::ND_CALL)
        ELEM(DGElementType::ND_ARTIFICIAL)
    default:
        assert(false && "unknown node type");
        return "Unknown type";
    };
#undef ELEM
}

class DependenceGraph;

class DGElement {
    unsigned _id{0};
    DGElementType _type;
    DependenceGraph &_dg;

  protected:
    friend class DependenceGraph;
    // Only for the use in ctor. This method gets the ID of this node
    // from the DependenceGraph (increasing the graph's id counter).
    static unsigned getNewID(DependenceGraph &g);

  public:
    virtual ~DGElement() = default;

    DGElement(DependenceGraph &dg, DGElementType t);

    DGElementType getType() const { return _type; }
    unsigned getID() const { return _id; }

    const DependenceGraph &getDG() const { return _dg; }
    DependenceGraph &getDG() { return _dg; }

#ifndef NDEBUG
    virtual void dump() const { std::cout << DGElemTypeToCString(getType()); }

    // verbose dump
    void dumpv() const {
        dump();
        std::cout << "\n";
    }
#endif // not NDEBUG
};

} // namespace sdg

// check type of node
template <sdg::DGElementType T>
bool isa(sdg::DGElement *n) {
    return n->getType() == T;
}

} // namespace dg

#endif
