#ifndef DG_LLVM_CDGRAPH_H_
#define DG_LLVM_CDGRAPH_H_

#include <cassert>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "dg/BBlockBase.h"

namespace dg {

//////////////////////////////////////////////////////////////////
/// Basic graph elements from which all other elements inherit.
//////////////////////////////////////////////////////////////////

/////
/// The basic class for the graph. It is either an instruction or a block,
/// depending on the chosen granularity of CFG.
/////
class CDNode;
class CDGraph;
class CDNode : public ElemWithEdges<CDNode> {
    friend class CDGraph;

    unsigned _id;
    CDNode(unsigned id) : _id(id) {}

  public:
    unsigned getID() const { return _id; }
};

/////
/// CDGraph - a graph that is used for the computation of control dependencies.
/// It contains nodes that correspond either to basic blocks or instructions
/// along with the successor relation.
/////
class CDGraph {
    using NodesVecT = std::vector<std::unique_ptr<CDNode>>;
    // using NodesPtrVecT = ADT::SparseBitvector;
    // FIXME ^^
    using PredicatesT = std::set<CDNode *>;

    std::string _name;
    NodesVecT _nodes;
    // NodesPtrVecT _predicates;
    PredicatesT _predicates;

    // iterator over the subgraphs that unwraps the unique_ptr
    struct node_iterator : public NodesVecT::iterator {
        using ContainedType = typename std::remove_reference<decltype(*(
                std::declval<NodesVecT::iterator>()->get()))>::type;

        node_iterator(const typename NodesVecT::iterator &it)
                : NodesVecT::iterator(it) {}
        node_iterator(const node_iterator &) = default;
        node_iterator() = default;

        ContainedType *operator*() {
            return (NodesVecT::iterator::operator*()).get();
        };
        ContainedType *operator->() {
            return ((NodesVecT::iterator::operator*()).get());
        };
    };

    struct nodes_range {
        NodesVecT &nodes;
        nodes_range(NodesVecT &b) : nodes(b) {}

        node_iterator begin() { return node_iterator(nodes.begin()); }
        node_iterator end() { return node_iterator(nodes.end()); }
    };

    // iterator over the predicates
    /*
    struct predicate_iterator : public NodesPtrVecT::const_iterator {
        NodesVecT& _nodes;

        predicate_iterator(NodesVecT& nodes, const typename
    NodesPtrVecT::const_iterator& it) : NodesPtrVecT::const_iterator(it),
    _nodes(nodes) {} predicate_iterator(const predicate_iterator&) = default;

        CDNode *operator*() {
            auto id = NodesPtrVecT::const_iterator::operator*();
            assert(id - 1 < _nodes.size());
            return this->_nodes[id - 1].get();
        };
        CDNode *operator->() {
            auto id = NodesPtrVecT::const_iterator::operator*();
            assert(id - 1 < _nodes.size());
            return this->_nodes[id - 1].get();
        };
    };

    struct predicates_range {
        NodesVecT& nodes;
        NodesPtrVecT& predicates;
        predicates_range(NodesVecT& n, NodesPtrVecT& b) : nodes(n),
    predicates(b) {}

        predicate_iterator begin() { return predicate_iterator(nodes,
    predicates.begin()); } predicate_iterator end() { return
    predicate_iterator(nodes, predicates.end()); }
    };
    */

  public:
    CDGraph(std::string name = "") : _name(std::move(name)) {}
    CDGraph(const CDGraph &rhs) = delete;
    CDGraph &operator=(const CDGraph &) = delete;
    CDGraph(CDGraph &&) = default;
    CDGraph &operator=(CDGraph &&) = default;

    CDNode &createNode() {
        auto *nd = new CDNode(_nodes.size() + 1);
        _nodes.emplace_back(nd);
        assert(_nodes.back()->getID() == _nodes.size() &&
               "BUG: we rely on the ordering by ids");
        return *nd;
    }

    // add an edge between two nodes in the graph.
    // This method registers also what nodes have more than successor
    void addNodeSuccessor(CDNode &nd, CDNode &succ) {
        nd.addSuccessor(&succ);
        if (nd.successors().size() > 1) {
            _predicates.insert(&nd);
        }
    }

    node_iterator begin() { return node_iterator(_nodes.begin()); }
    node_iterator end() { return node_iterator(_nodes.end()); }
    nodes_range nodes() { return {_nodes}; }

    CDNode *getNode(unsigned id) {
        assert(id - 1 < _nodes.size());
        return _nodes[id - 1].get();
    }

    const CDNode *getNode(unsigned id) const {
        assert(id - 1 < _nodes.size());
        return _nodes[id - 1].get();
    }

    size_t size() const { return _nodes.size(); }
    bool empty() const { return _nodes.empty(); }

    /*
    predicate_iterator predicates_begin() { return predicate_iterator(_nodes,
    _predicates.begin()); } predicate_iterator predicates_end() { return
    predicate_iterator(_nodes, _predicates.end()); } predicates_range
    predicates() { return predicates_range(_nodes, _predicates); }
    */

    decltype(_predicates.begin()) predicates_begin() {
        return _predicates.begin();
    }
    decltype(_predicates.end()) predicates_end() { return _predicates.end(); }
    decltype(_predicates.begin()) predicates_begin() const {
        return _predicates.begin();
    }
    decltype(_predicates.end()) predicates_end() const {
        return _predicates.end();
    }
    PredicatesT &predicates() { return _predicates; }
    const PredicatesT &predicates() const { return _predicates; }

    /*
    bool isPredicate(const CDNode& nd) const {
        return _predicates.get(nd.getID());
    }
    */

    bool isPredicate(const CDNode &nd) const {
        return _predicates.count(const_cast<CDNode *>(&nd)) > 0;
    }

    const std::string &getName() const { return _name; }
};

} // namespace dg

#endif
