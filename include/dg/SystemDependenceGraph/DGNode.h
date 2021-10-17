#ifndef DG_DG_NODE_H_
#define DG_DG_NODE_H_

#include <cassert>

#include "DepDGElement.h"
#include "dg/ADT/DGContainer.h"

namespace dg {
namespace sdg {

class DGBBlock;

class DGNode : public DepDGElement {
    DGBBlock *_bblock{nullptr};

  protected:
    DGNode(DependenceGraph &g, DGElementType t);

  public:
    ///
    // Assign a BBlock to the node. Having BBlocks in SDG is optional,
    // but usually useful (we can merge control dependencies of nodes).
    void setBBlock(DGBBlock *g) { _bblock = g; }
    const DGBBlock *getBBlock() const { return _bblock; }
    DGBBlock *getBBlock() { return _bblock; }

    static DGNode *get(DGElement *n) {
        switch (n->getType()) {
        case DGElementType::ND_INSTRUCTION:
        case DGElementType::ND_CALL:
        case DGElementType::ND_ARGUMENT:
        case DGElementType::ND_ARTIFICIAL:
            return static_cast<DGNode *>(n);
        default:
            return nullptr;
        }
    }

#ifndef NDEBUG
    void dump() const override {
        std::cout << "<" << getID() << "> ";
        DGElement::dump();
    }
#endif // not NDEBUG
};

/// ----------------------------------------------------------------------
// Instruction
/// ----------------------------------------------------------------------
class DGNodeInstruction : public DGNode {
  public:
    DGNodeInstruction(DependenceGraph &g)
            : DGNode(g, DGElementType::ND_INSTRUCTION) {}

    static DGNodeInstruction *get(DGElement *n) {
        return isa<DGElementType::ND_INSTRUCTION>(n)
                       ? static_cast<DGNodeInstruction *>(n)
                       : nullptr;
    }
};

/// ----------------------------------------------------------------------
// Argument
/// ----------------------------------------------------------------------
class DGNodeArgument : public DGNode {
    // parameter in edges
    EdgesContainer<DepDGElement> _in_edges;
    EdgesContainer<DepDGElement> _rev_in_edges;
    // parameter out edges
    EdgesContainer<DepDGElement> _out_edges;
    EdgesContainer<DepDGElement> _rev_out_edges;

    using edge_iterator = DepDGElement::edge_iterator;
    using const_edge_iterator = DepDGElement::const_edge_iterator;
    using edges_range = DepDGElement::edges_range;
    using const_edges_range = DepDGElement::const_edges_range;

  public:
    DGNodeArgument(DependenceGraph &g)
            : DGNode(g, DGElementType::ND_ARGUMENT) {}

    static DGNodeArgument *get(DGElement *n) {
        return isa<DGElementType::ND_ARGUMENT>(n)
                       ? static_cast<DGNodeArgument *>(n)
                       : nullptr;
    }

    edge_iterator parameter_in_begin() { return _in_edges.begin(); }
    edge_iterator parameter_in_end() { return _in_edges.end(); }
    edge_iterator parameter_rev_in_begin() { return _rev_in_edges.begin(); }
    edge_iterator parameter_rev_in_end() { return _rev_in_edges.end(); }
    const_edge_iterator parameter_in_begin() const { return _in_edges.begin(); }
    const_edge_iterator parameter_in_end() const { return _in_edges.end(); }
    const_edge_iterator parameter_rev_in_begin() const {
        return _rev_in_edges.begin();
    }
    const_edge_iterator parameter_rev_in_end() const {
        return _rev_in_edges.end();
    }

    edges_range parameter_in() { return {_in_edges}; }
    const_edges_range parameter_in() const { return {_in_edges}; }
    edges_range parameter_rev_in() { return {_rev_in_edges}; }
    const_edges_range parameter_rev_in() const { return {_rev_in_edges}; }

    edge_iterator parameter_out_begin() { return _out_edges.begin(); }
    edge_iterator parameter_out_end() { return _out_edges.end(); }
    edge_iterator parameter_rev_out_begin() { return _rev_out_edges.begin(); }
    edge_iterator parameter_rev_out_end() { return _rev_out_edges.end(); }
    const_edge_iterator parameter_out_begin() const {
        return _out_edges.begin();
    }
    const_edge_iterator parameter_out_end() const { return _out_edges.end(); }
    const_edge_iterator parameter_rev_out_begin() const {
        return _rev_out_edges.begin();
    }
    const_edge_iterator parameter_rev_out_end() const {
        return _rev_out_edges.end();
    }

    edges_range parameter_out() { return {_out_edges}; }
    const_edges_range parameter_out() const { return {_out_edges}; }
    edges_range parameter_rev_out() { return {_rev_out_edges}; }
    const_edges_range parameter_rev_out() const { return {_rev_out_edges}; }
};

/// ----------------------------------------------------------------------
// Artificial node (e.g., vararg node, noreturn node,
// unified return node, etc.)
/// ----------------------------------------------------------------------
class DGNodeArtificial : public DGNode {
  public:
    DGNodeArtificial(DependenceGraph &g)
            : DGNode(g, DGElementType::ND_ARTIFICIAL) {}
};

} // namespace sdg
} // namespace dg

#endif
