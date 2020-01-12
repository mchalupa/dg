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
        INVALID=0,
        /// special elements
        // Pair of arguments (input & output)
        ARG_PAIR = 1,
        // nodes
        ND_INSTRUCTION = 2,
        ND_ARGUMENT,
        ND_CALL,
        ND_ARTIFICIAL
};


inline const char *DGElemTypeToCString(enum DGElementType type)
{
#define ELEM(t) case t: do {return (#t); }while(0); break;
    switch(type) {
        ELEM(DGElementType::INVALID)
        ELEM(DGElementType::ARG_PAIR)
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
    DGElementType _type;
    DependenceGraph& _dg;

public:
    virtual ~DGElement() = default;

    DGElement(DependenceGraph& dg, DGElementType t) : _type(t), _dg(dg) {}

    DGElementType getType() const { return _type; }

    const DependenceGraph& getDG() const { return _dg; }
    DependenceGraph& getDG() { return _dg; }

#ifndef NDEBUG
    virtual void dump() const {
        std::cout << DGElemTypeToCString(getType());
    }

    // verbose dump
    void dumpv() const {
        dump();
        std::cout << "\n";
    }
#endif // not NDEBUG
};

} // namespace sdg


// check type of node
template <sdg::DGElementType T> bool isa(sdg::DGElement *n) {
    return n->getType() == T;
}

} // namespace dg

#endif
