#ifndef DG_DEPENDENCIES_ELEM_H_
#define DG_DEPENDENCIES_ELEM_H_

#include "DGElement.h"
#include "dg/ADT/DGContainer.h"

namespace dg {
namespace sdg {

class DependenceGraph;
class DGNodeArgument;

///
// An element of the graph that can have dependencies
//
// FIXME: split to data and control classes,
// so that e.g., basic blocks do not bear the memory dependencies.
// It is a waste of memory.
class DepDGElement : public DGElement {
    // nodes that use this node as operand
    EdgesContainer<DepDGElement> _use_deps;
    // nodes that write to memory that this node reads
    EdgesContainer<DepDGElement> _memory_deps;
    // control dependencies
    EdgesContainer<DepDGElement> _control_deps;

    // reverse containers
    EdgesContainer<DepDGElement> _rev_use_deps;
    EdgesContainer<DepDGElement> _rev_memory_deps;
    EdgesContainer<DepDGElement> _rev_control_deps;

  protected:
    using edge_iterator = EdgesContainer<DepDGElement>::iterator;
    using const_edge_iterator = EdgesContainer<DepDGElement>::const_iterator;

    class edges_range {
        friend class DepDGElement;
        friend class DGNodeArgument;
        EdgesContainer<DepDGElement> &_C;

        edges_range(EdgesContainer<DepDGElement> &C) : _C(C) {}

      public:
        edge_iterator begin() { return _C.begin(); }
        edge_iterator end() { return _C.end(); }
    };

    class const_edges_range {
        friend class DepDGElement;
        friend class DGNodeArgument;
        const EdgesContainer<DepDGElement> &_C;

        const_edges_range(const EdgesContainer<DepDGElement> &C) : _C(C) {}

      public:
        const_edge_iterator begin() const { return _C.begin(); }
        const_edge_iterator end() const { return _C.end(); }
    };

    // FIXME: add data deps iterator = use + memory
    //

    DepDGElement(DependenceGraph &g, DGElementType type) : DGElement(g, type) {}

  public:
    static DepDGElement *get(DGElement *elem) {
        if (elem->getType() == DGElementType::BBLOCK ||
            elem->getType() >= DGElementType::NODE)
            return static_cast<DepDGElement *>(elem);
        return nullptr;
    }

    /// add user of this node (edge 'this'->'nd')
    void addUser(DepDGElement &nd) {
        _use_deps.insert(&nd);
        nd._rev_use_deps.insert(this);
    }

    /// this node uses nd (the edge 'nd'->'this')
    void addUses(DepDGElement &nd) { nd.addUser(*this); }

    // this node reads values from 'nd' (the edge 'nd' -> 'this')
    void addMemoryDep(DepDGElement &nd) {
        _memory_deps.insert(&nd);
        nd._rev_memory_deps.insert(this);
    }

    // this node is control dependent on 'nd' (the edge 'nd' -> 'this')
    void addControlDep(DepDGElement &nd) {
        _control_deps.insert(&nd);
        nd._rev_control_deps.insert(this);
    }

    // this node controls 'nd' (the edge 'this' -> 'nd')
    void addControls(DepDGElement &nd) { nd.addControlDep(*this); }

    // use dependencies
    edge_iterator uses_begin() { return _use_deps.begin(); }
    edge_iterator uses_end() { return _use_deps.end(); }
    edge_iterator users_begin() { return _rev_use_deps.begin(); }
    edge_iterator users_end() { return _rev_use_deps.end(); }
    const_edge_iterator uses_begin() const { return _use_deps.begin(); }
    const_edge_iterator uses_end() const { return _use_deps.end(); }
    const_edge_iterator users_begin() const { return _rev_use_deps.begin(); }
    const_edge_iterator users_end() const { return _rev_use_deps.end(); }

    edges_range uses() { return {_use_deps}; }
    const_edges_range uses() const { return {_use_deps}; }
    edges_range users() { return {_rev_use_deps}; }
    const_edges_range users() const { return {_rev_use_deps}; }

    // memory dependencies
    edge_iterator memdep_begin() { return _memory_deps.begin(); }
    edge_iterator memdep_end() { return _memory_deps.end(); }
    edge_iterator rev_memdep_begin() { return _rev_memory_deps.begin(); }
    edge_iterator rev_memdep_end() { return _rev_memory_deps.end(); }
    const_edge_iterator memdep_begin() const { return _memory_deps.begin(); }
    const_edge_iterator memdep_end() const { return _memory_deps.end(); }
    const_edge_iterator rev_memdep_begin() const {
        return _rev_memory_deps.begin();
    }
    const_edge_iterator rev_memdep_end() const {
        return _rev_memory_deps.end();
    }

    edges_range memdep() { return {_memory_deps}; }
    const_edges_range memdep() const { return {_memory_deps}; }
    edges_range rev_memdep() { return {_rev_memory_deps}; }
    const_edges_range rev_memdep() const { return {_rev_memory_deps}; }

    // FIXME: add datadep iterator = memdep + uses

    // control dependencies
    edge_iterator control_dep_begin() { return _control_deps.begin(); }
    edge_iterator control_dep_end() { return _control_deps.end(); }
    edge_iterator controls_begin() { return _rev_control_deps.begin(); }
    edge_iterator controls_dep_end() { return _rev_control_deps.end(); }
    const_edge_iterator control_dep_begin() const {
        return _control_deps.begin();
    }
    const_edge_iterator control_dep_end() const { return _control_deps.end(); }
    const_edge_iterator controls_begin() const {
        return _rev_control_deps.begin();
    }
    const_edge_iterator controls_end() const { return _rev_control_deps.end(); }

    edges_range control_deps() { return {_control_deps}; }
    const_edges_range control_deps() const { return {_control_deps}; }
    edges_range controls() { return {_rev_control_deps}; }
    const_edges_range controls() const { return {_rev_control_deps}; }
};

} // namespace sdg
} // namespace dg

#endif
