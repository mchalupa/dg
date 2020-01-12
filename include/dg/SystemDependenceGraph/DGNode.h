#ifndef DG_DG_NODE_H_
#define DG_DG_NODE_H_

#include <cassert>

#include "DGElement.h"
#include "dg/ADT/DGContainer.h"

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

    using edge_iterator = EdgesContainer<DGNode>::iterator;
    using const_edge_iterator = EdgesContainer<DGNode>::const_iterator;

    // nodes that use this node as operand
    EdgesContainer<DGNode> _use_deps;
    // nodes that write to memory that this node reads 
    EdgesContainer<DGNode> _memory_deps;
    // control dependencies
    EdgesContainer<DGNode> _control_deps;

    // reverse containers
    EdgesContainer<DGNode> _rev_use_deps;
    EdgesContainer<DGNode> _rev_memory_deps;
    EdgesContainer<DGNode> _rev_control_deps;

    class edges_range {
        friend class DGNode;
        EdgesContainer<DGNode>& _C;

        edges_range(EdgesContainer<DGNode>& C) : _C(C) {}
    public:
        edge_iterator begin() { return _C.begin(); }
        edge_iterator end() { return _C.end(); }
    };

    class const_edges_range {
        friend class DGNode;
        const EdgesContainer<DGNode>& _C;

        const_edges_range(const EdgesContainer<DGNode>& C) : _C(C) {}
    public:
        const_edge_iterator begin() const { return _C.begin(); }
        const_edge_iterator end() const { return _C.end(); }
    };

    // FIXME: add data deps iterator = use + memory
    //

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

    /// add user of this node (edge 'this'->'nd')
    void addUser(DGNode& nd) {
        _use_deps.insert(&nd);
        nd._rev_use_deps.insert(this);
    }

    /// this node uses nd (the edge 'nd'->'this')
    void addUses(DGNode& nd) {
        nd.addUser(*this);
    }

    edge_iterator uses_begin() { return _use_deps.begin(); }
    edge_iterator uses_end() { return _use_deps.end(); }
    edge_iterator users_begin() { return _rev_use_deps.begin(); }
    edge_iterator users_end() { return _rev_use_deps.end(); }
    const_edge_iterator uses_begin() const { return _use_deps.begin(); }
    const_edge_iterator uses_end() const { return _use_deps.end(); }
    const_edge_iterator users_begin() const { return _rev_use_deps.begin(); }
    const_edge_iterator users_end() const { return _rev_use_deps.end(); }

    edges_range uses() { return edges_range(_use_deps); }
    const_edges_range uses() const { return const_edges_range(_use_deps); }
    edges_range users() { return edges_range(_rev_use_deps); }
    const_edges_range users() const { return const_edges_range(_rev_use_deps); }


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
