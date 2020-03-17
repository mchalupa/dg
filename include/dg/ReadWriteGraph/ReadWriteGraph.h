#ifndef DG_READ_WRITE_GRAPH_H_
#define DG_READ_WRITE_GRAPH_H_

#include <vector>
#include <memory>

#include "dg/BFS.h"
#include "dg/ReadWriteGraph/RWNode.h"
#include "dg/ReadWriteGraph/RWBBlock.h"
#include "dg/ReadWriteGraph/RWSubgraph.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

class ReadWriteGraph {
    // FIXME: get rid of this
    unsigned int dfsnum{1};

    size_t lastNodeID{0};
    using NodesT = std::vector<std::unique_ptr<RWNode>>;
    using SubgraphsT = std::vector<std::unique_ptr<RWSubgraph>>;

    NodesT _nodes;
    SubgraphsT _subgraphs;
    RWSubgraph *_entry{nullptr};

    // iterator over the bsubgraphs that returns the bsubgraph,
    // not the unique_ptr to the bsubgraph
    struct subgraph_iterator : public SubgraphsT::iterator {
        using ContainedType
            = std::remove_reference<
                decltype(*(std::declval<SubgraphsT::iterator>()->get()))
                                   >::type;

        subgraph_iterator(const SubgraphsT::iterator& it)
        : SubgraphsT::iterator(it) {}

        ContainedType *operator*() {
            return (SubgraphsT::iterator::operator*()).get();
        };
        ContainedType *operator->() {
            return ((SubgraphsT::iterator::operator*()).get());
        };
    };

    struct subgraphs_range {
        SubgraphsT& subgraphs;
        subgraphs_range(SubgraphsT& b) : subgraphs(b) {}

        subgraph_iterator begin() { return subgraph_iterator(subgraphs.begin()); }
        subgraph_iterator end() { return subgraph_iterator(subgraphs.end()); }
    };


public:
    ReadWriteGraph() = default;
    ReadWriteGraph(ReadWriteGraph&&) = default;
    ReadWriteGraph& operator=(ReadWriteGraph&&) = default;

    RWSubgraph *getEntry() { return _entry; }
    const RWSubgraph *getEntry() const { return _entry; }
    void setEntry(RWSubgraph *e) { _entry = e; }

    void removeUselessNodes();

    void optimize() {
        removeUselessNodes();
    }

    RWNode& create(RWNodeType t) {
      if (t == RWNodeType::CALL) {
        _nodes.emplace_back(new RWNodeCall(++lastNodeID));
      } else {
        _nodes.emplace_back(new RWNode(++lastNodeID, t));
      }
      return *_nodes.back().get();
    }

    RWSubgraph& createSubgraph() {
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

    subgraph_iterator subgraphs_begin() {
        return subgraph_iterator(_subgraphs.begin());
    }
    subgraph_iterator subgraphs_end() {
        return subgraph_iterator(_subgraphs.end());
    }

    subgraphs_range subgraphs() { return subgraphs_range(_subgraphs); }

    // get nodes in BFS order and store them into
    // the container
    /*
    template <typename ContainerOrNode>
    std::vector<RWNode *> getNodes(const ContainerOrNode& start,
                                   unsigned expected_num = 0)
    {
        ++dfsnum;

        std::vector<RWNode *> cont;
        if (expected_num != 0)
            cont.reserve(expected_num);

        struct DfsIdTracker {
            const unsigned dfsnum;
            DfsIdTracker(unsigned dnum) : dfsnum(dnum) {}

            void visit(RWNode *n) { n->dfsid = dfsnum; }
            bool visited(RWNode *n) const { return n->dfsid == dfsnum; }
        };

        DfsIdTracker visitTracker(dfsnum);
        BFS<RWNode, DfsIdTracker> bfs(visitTracker);

        bfs.run(start,
                [&cont](RWNode *n) {
                    cont.push_back(n);
                });

        return cont;
    }
    */
};

} // namespace dda
} // namespace dg

#endif // DG_READ_WRITE_GRAPH_H_
