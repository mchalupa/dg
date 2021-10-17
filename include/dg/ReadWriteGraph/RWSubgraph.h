#ifndef DG_READ_WRITE_SUBRAPH_H_
#define DG_READ_WRITE_SUBRAPH_H_

#include <memory>
#include <vector>

#include "RWBBlock.h"
#include "dg/util/iterators.h"

namespace dg {
namespace dda {

class ReadWriteGraph;
class RWNode;

class RWSubgraph {
    // FIXME: get rid of this
    // unsigned int dfsnum{1};

    using BBlocksVecT = std::vector<std::unique_ptr<RWBBlock>>;

    // iterator over the bblocks that returns the bblock,
    // not the unique_ptr to the bblock
    struct block_iterator : public BBlocksVecT::iterator {
        using ContainedType = std::remove_reference<decltype(*(
                std::declval<BBlocksVecT::iterator>()->get()))>::type;

        block_iterator(const BBlocksVecT::iterator &it)
                : BBlocksVecT::iterator(it) {}
        block_iterator(const block_iterator &) = default;
        block_iterator() = default;

        ContainedType *operator*() {
            return (BBlocksVecT::iterator::operator*()).get();
        };
        ContainedType *operator->() {
            return ((BBlocksVecT::iterator::operator*()).get());
        };
    };

    BBlocksVecT _bblocks;

    struct blocks_range {
        BBlocksVecT &blocks;
        blocks_range(BBlocksVecT &b) : blocks(b) {}

        block_iterator begin() { return block_iterator(blocks.begin()); }
        block_iterator end() { return block_iterator(blocks.end()); }
    };

    // Build blocks for the nodes. If 'dce' is set to true,
    // the dead code is eliminated after building the blocks.
    void buildBBlocks(bool dce = false);

    friend class ReadWriteGraph;

    std::vector<RWNode *> _callers;

    // for debugging
    std::string name;

  public:
    RWSubgraph() = default;
    RWSubgraph(RWSubgraph &&) = default;
    RWSubgraph &operator=(RWSubgraph &&) = default;

    RWNode *getRoot() { return _bblocks.front()->getFirst(); }
    const RWNode *getRoot() const { return _bblocks.front()->getFirst(); }

    void setName(const std::string &nm) { name = nm; }
    const std::string &getName() const { return name; }

    RWBBlock &createBBlock() {
        _bblocks.emplace_back(new RWBBlock(this));
        return *_bblocks.back().get();
    }

    bool hasCaller(RWNode *c) const {
        return dg::any_of(_callers, [c](RWNode *tmp) { return tmp == c; });
    }

    void splitBBlocksOnCalls();
    void addCaller(RWNode *c) {
        assert(c->getType() == RWNodeType::CALL);
        if (hasCaller(c)) {
            return;
        }
        _callers.push_back(c);
    }

    std::vector<RWNode *> &getCallers() { return _callers; }
    const std::vector<RWNode *> &getCallers() const { return _callers; }

    const BBlocksVecT &getBBlocks() const { return _bblocks; }

    block_iterator bblocks_begin() { return {_bblocks.begin()}; }
    block_iterator bblocks_end() { return {_bblocks.end()}; }
    blocks_range bblocks() { return {_bblocks}; }

    auto size() const -> decltype(_bblocks.size()) { return _bblocks.size(); }
};

} // namespace dda
} // namespace dg

#endif
