#ifndef DG_DG_NODE_H_
#define DG_DG_NODE_H_

#include <cassert>
#include "DGElement.h"

namespace dg {
namespace sdg {

class DGBBlock;

class DGNode : public DGElement {
    unsigned _id{0};
    DGBBlock *_bblock{nullptr};

    // Only for the use in ctor. This method gets the ID of this node
    // from the DependenceGraph (increasing the graph's id counter).
    friend class DependenceGraph;
    unsigned getNewID(DependenceGraph& g);

protected:
    DGNode(DependenceGraph& g, DGElementType t);

public:
    ///
    // Assign a BBlock to the node. Having BBlocks in SDG is optional,
    // but usually useful (we can merge control dependencies of nodes).
    void setBBlock(DGBBlock *g) { _bblock = g; }
    const DGBBlock* getBBlock() const { return _bblock; }
    DGBBlock* getBBlock() { return _bblock; }

    unsigned getID() const { return _id; }

#ifndef NDEBUG
    void dump() const override {
        std::cout << "<"<< getID() << "> ";
        DGElement::dump();
    }
#endif // not NDEBUG
};

/// ----------------------------------------------------------------------
// Instruction
/// ----------------------------------------------------------------------
class DGNodeInstruction : public DGNode {
public:
    DGNodeInstruction(DependenceGraph& g)
    : DGNode(g, DGElementType::ND_INSTRUCTION) {}

    static DGNodeInstruction *get(DGElement *n) {
        return isa<DGElementType::ND_INSTRUCTION>(n) ?
            static_cast<DGNodeInstruction *>(n) : nullptr;
    }
};

/// ----------------------------------------------------------------------
// Argument
/// ----------------------------------------------------------------------
class DGNodeArgument : public DGNode {
public:
    DGNodeArgument(DependenceGraph& g) : DGNode(g, DGElementType::ND_ARGUMENT) {}

    static DGNodeArgument *get(DGElement *n) {
        return isa<DGElementType::ND_ARGUMENT>(n) ?
            static_cast<DGNodeArgument *>(n) : nullptr;
    }
};

/// ----------------------------------------------------------------------
// Artificial node (e.g., vararg node, noreturn node,
// unified return node, etc.)
/// ----------------------------------------------------------------------
class DGNodeArtificial : public DGNode {
public:
    DGNodeArtificial(DependenceGraph& g)
    : DGNode(g, DGElementType::ND_ARTIFICIAL) {}
};

} // namespace sdg
} // namespace dg

#endif
