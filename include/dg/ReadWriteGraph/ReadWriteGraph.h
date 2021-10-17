#ifndef DG_READ_WRITE_GRAPH_H_
#define DG_READ_WRITE_GRAPH_H_

#include <memory>
#include <vector>

#include "dg/BFS.h"
#include "dg/ReadWriteGraph/RWBBlock.h"
#include "dg/ReadWriteGraph/RWNode.h"
#include "dg/ReadWriteGraph/RWSubgraph.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

class ReadWriteGraph {
    size_t lastNodeID{0};
    using NodesT = std::vector<std::unique_ptr<RWNode>>;
    using SubgraphsT = std::vector<std::unique_ptr<RWSubgraph>>;

    NodesT _nodes;
    SubgraphsT _subgraphs;
    RWSubgraph *_entry{nullptr};

    // iterator over the bsubgraphs that returns the bsubgraph,
    // not the unique_ptr to the bsubgraph
    struct subgraph_iterator : public SubgraphsT::iterator {
        using ContainedType = std::remove_reference<decltype(*(
                std::declval<SubgraphsT::iterator>()->get()))>::type;

        subgraph_iterator(const SubgraphsT::iterator &it)
                : SubgraphsT::iterator(it) {}

        ContainedType *operator*() {
            return (SubgraphsT::iterator::operator*()).get();
        };
        ContainedType *operator->() {
            return ((SubgraphsT::iterator::operator*()).get());
        };
    };

    struct subgraphs_range {
        SubgraphsT &subgraphs;
        subgraphs_range(SubgraphsT &b) : subgraphs(b) {}

        subgraph_iterator begin() { return {subgraphs.begin()}; }
        subgraph_iterator end() { return {subgraphs.end()}; }
    };

  public:
    ReadWriteGraph() = default;
    ReadWriteGraph(ReadWriteGraph &&) = default;
    ReadWriteGraph &operator=(ReadWriteGraph &&) = default;

    RWSubgraph *getEntry() { return _entry; }
    const RWSubgraph *getEntry() const { return _entry; }
    void setEntry(RWSubgraph *e) { _entry = e; }

    void removeUselessNodes();

    void optimize() { removeUselessNodes(); }

    RWNode *getNode(unsigned id) {
        assert(id - 1 < _nodes.size());
        auto *n = _nodes[id - 1].get();
        assert(n->getID() == id);
        return n;
    }

    const RWNode *getNode(unsigned id) const {
        assert(id - 1 < _nodes.size());
        auto *n = _nodes[id - 1].get();
        assert(n->getID() == id);
        return n;
    }

    RWNode &create(RWNodeType t) {
        if (t == RWNodeType::CALL) {
            _nodes.emplace_back(new RWNodeCall(++lastNodeID));
        } else {
            _nodes.emplace_back(new RWNode(++lastNodeID, t));
        }
        return *_nodes.back().get();
    }

    RWSubgraph &createSubgraph() {
        _subgraphs.emplace_back(new RWSubgraph());
        return *_subgraphs.back().get();
    }

    // Build blocks for the nodes. If 'dce' is set to true,
    // the dead code is eliminated after building the blocks.
    /*
    void buildBBlocks(bool dce = false) {
        for (auto& s : _subgraphs) {
            s->buildBBlocks(dce);
        }
    }
    */

    void splitBBlocksOnCalls() {
        for (auto &s : _subgraphs) {
            s->splitBBlocksOnCalls();
        }
    }

    subgraph_iterator subgraphs_begin() { return {_subgraphs.begin()}; }
    subgraph_iterator subgraphs_end() { return {_subgraphs.end()}; }

    subgraphs_range subgraphs() { return {_subgraphs}; }

    auto size() const -> decltype(_subgraphs.size()) {
        return _subgraphs.size();
    }
};

} // namespace dda
} // namespace dg

#endif // DG_READ_WRITE_GRAPH_H_
