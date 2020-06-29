#ifndef DG_LLVM_CDGRAPH_H_
#define DG_LLVM_CDGRAPH_H_

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
class CDNode : public CFGElement<CDNode> {
};

/////
/// CDGraph - a graph that is used for the computation of control dependencies.
/// It contains nodes that correspond either to basic blocks or instructions
/// along with the successor relation.
/////
class CDGraph {
    using NodesVecT = std::vector<std::unique_ptr<CDNode>>;

    std::string _name;
    NodesVecT _nodes;

    // iterator over the subgraphs that unwraps the unique_ptr
    struct node_iterator : public NodesVecT::iterator {
        using ContainedType
            = typename std::remove_reference<decltype(*(std::declval<NodesVecT::iterator>()->get()))>::type;

        node_iterator(const typename NodesVecT::iterator& it) : NodesVecT::iterator(it) {}
        node_iterator(const node_iterator&) = default;
        node_iterator() = default;

        ContainedType *operator*() {
            return (NodesVecT::iterator::operator*()).get();
        };
        ContainedType *operator->() {
            return ((NodesVecT::iterator::operator*()).get());
        };
    };

    struct nodes_range {
        NodesVecT& nodes;
        nodes_range(NodesVecT& b) : nodes(b) {}

        node_iterator begin() { return node_iterator(nodes.begin()); }
        node_iterator end() { return node_iterator(nodes.end()); }
    };

public:
    CDGraph(const std::string& name = "") : _name(name) {}
    CDGraph(const CDGraph& rhs) = delete;
    CDGraph(CDGraph&& rhs) = default;

    CDNode& createNode() {
        auto *nd = new CDNode();
        _nodes.emplace_back(nd);
        return *nd;
    }

    node_iterator begin() { return node_iterator(_nodes.begin()); }
    node_iterator end() { return node_iterator(_nodes.end()); }
    nodes_range nodes() { return nodes_range(_nodes); }

    const std::string& getName() const { return _name; }
};

} // namespace dg

#endif
