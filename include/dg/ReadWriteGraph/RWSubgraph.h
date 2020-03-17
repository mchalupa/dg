#ifndef DG_READ_WRITE_SUBRAPH_H_
#define DG_READ_WRITE_SUBRAPH_H_

#include <vector>
#include <memory>

namespace dg {
namespace dda {

class ReadWriteGraph;
class RWNode;

class RWSubgraph {
    // FIXME: get rid of this
    //unsigned int dfsnum{1};

    using BBlocksVecT = std::vector<std::unique_ptr<RWBBlock>>;

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

    // Build blocks for the nodes. If 'dce' is set to true,
    // the dead code is eliminated after building the blocks.
    void buildBBlocks(bool dce = false);

    friend class ReadWriteGraph;

public:
    RWSubgraph() = default;
    RWSubgraph(RWSubgraph&&) = default;
    RWSubgraph& operator=(RWSubgraph&&) = default;

    RWNode *getRoot() { return _bblocks.front()->getFirst(); }
    const RWNode *getRoot() const { return _bblocks.front()->getFirst(); }

    RWBBlock& createBBlock() {
        _bblocks.emplace_back(new RWBBlock());
        return *_bblocks.back().get();
    }

    const BBlocksVecT& getBBlocks() const { return _bblocks; }

    block_iterator bblocks_begin() { return block_iterator(_bblocks.begin()); }
    block_iterator bblocks_end() { return block_iterator(_bblocks.end()); }

    blocks_range bblocks() { return blocks_range(_bblocks); }
};

} // namespace dda
} // namespace dg



#endif
