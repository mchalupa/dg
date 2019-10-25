#ifndef DG_READ_WRITE_GRAPH_H_
#define DG_READ_WRITE_GRAPH_H_

#include <vector>
#include <memory>

#include "dg/BFS.h"
#include "dg/ReadWriteGraph/RWNode.h"
#include "dg/ReadWriteGraph/RWBBlock.h"

#include "dg/util/debug.h"

namespace dg {
namespace dda {

class ReadWriteGraph {
    // FIXME: get rid of this
    unsigned int dfsnum{1};

    size_t lastNodeID{0};
    RWNode *root{nullptr};
    using BBlocksVecT = std::vector<std::unique_ptr<RWBBlock>>;
    using NodesT = std::vector<std::unique_ptr<RWNode>>;

    // iterator over the bblocks that returns the bblock,
    // not the unique_ptr to the bblock
    struct block_iterator : public BBlocksVecT::iterator {
        using ContainedType
            = std::remove_reference<decltype(*(std::declval<BBlocksVecT::iterator>()->get()))>::type;

        block_iterator(const BBlocksVecT::iterator& it) : BBlocksVecT::iterator(it) {}

        ContainedType *operator*() {
            return (BBlocksVecT::iterator::operator*()).get();
        };
        ContainedType *operator->() {
            return ((BBlocksVecT::iterator::operator*()).get());
        };
    };

    BBlocksVecT _bblocks;

    struct blocks_range {
        BBlocksVecT& blocks;
        blocks_range(BBlocksVecT& b) : blocks(b) {}

        block_iterator begin() { return block_iterator(blocks.begin()); }
        block_iterator end() { return block_iterator(blocks.end()); }
    };

    NodesT _nodes;

public:
    ReadWriteGraph() = default;
    ReadWriteGraph(RWNode *r) : root(r) {};
    ReadWriteGraph(ReadWriteGraph&&) = default;
    ReadWriteGraph& operator=(ReadWriteGraph&&) = default;

    RWNode *getRoot() const { return root; }
    void setRoot(RWNode *r) { root = r; }

    const std::vector<std::unique_ptr<RWBBlock>>& getBBlocks() const { return _bblocks; }

    block_iterator blocks_begin() { return block_iterator(_bblocks.begin()); }
    block_iterator blocks_end() { return block_iterator(_bblocks.end()); }

    blocks_range blocks() { return blocks_range(_bblocks); }

    void removeUselessNodes();

    void optimize() {
        removeUselessNodes();
    }

    RWNode *create(RWNodeType t) {
      _nodes.emplace_back(new RWNode(++lastNodeID, t));
      return _nodes.back().get();
    }

    // Build blocks for the nodes. If 'dce' is set to true,
    // the dead code is eliminated after building the blocks.
    void buildBBlocks(bool dce = false);

    // get nodes in BFS order and store them into
    // the container
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
};

} // namespace dda
} // namespace dg

#endif // DG_READ_WRITE_GRAPH_H_
