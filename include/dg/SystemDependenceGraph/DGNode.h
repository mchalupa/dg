#ifndef DG_DG_NODE_H_
#define DG_DG_NODE_H_

#include <cassert>
#include "DepDGElement.h"

namespace dg {
namespace sdg {

class DGBBlock;

class DGNode : public DepDGElement {
    DGBBlock *_bblock{nullptr};

protected:
    DGNode(DependenceGraph& g, DGElementType t);

public:
    ///
    // Assign a BBlock to the node. Having BBlocks in SDG is optional,
    // but usually useful (we can merge control dependencies of nodes).
    void setBBlock(DGBBlock *g) { _bblock = g; }
    const DGBBlock* getBBlock() const { return _bblock; }
    DGBBlock* getBBlock() { return _bblock; }

    static DGNode* get(DGElement *n) {
        switch(n->getType()) {
            case DGElementType::ND_INSTRUCTION:
            case DGElementType::ND_CALL:
            case DGElementType::ND_ARGUMENT:
            case DGElementType::ND_ARTIFICIAL:
                return static_cast<DGNode*>(n);
            default:
                return nullptr;
        }
    }

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
