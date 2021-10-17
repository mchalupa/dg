#ifndef DG_SUBGRAPHBASE_H_
#define DG_SUBGRAPHBASE_H_

namespace dg {

template <typename SubgraphT, typename BBlockT>
class SubgraphBase {
    using BBlocksVecT = std::vector<std::unique_ptr<BBlockT>>;

    // iterator over the bblocks that returns the bblock,
    // not the unique_ptr to the bblock
    struct block_iterator : public BBlocksVecT::iterator {
        using ContainedType = typename std::remove_reference<decltype(*(
                std::declval<BBlocksVecT::iterator>()->get()))>::type;

        block_iterator(const typename BBlocksVecT::iterator &it)
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

    // std::vector<typename BBlockT::NodeType *> _callers;

    // for debugging
    std::string name;

  public:
    SubgraphBase() = default;
    SubgraphBase(SubgraphBase &&) = default;
    SubgraphBase &operator=(SubgraphBase &&) = default;

    void setName(const std::string &nm) { name = nm; }
    const std::string &getName() const { return name; }

    BBlockT &createBBlock() {
        _bblocks.emplace_back(new BBlockT(static_cast<SubgraphT *>(this)));
        return *_bblocks.back().get();
    }

    /*
    void addCaller(RWNode *c) {
        assert(c->getType() == RWNodeType::CALL);
        for (auto *tmp : _callers) {
            if (tmp == c)
                return;
        }
        _callers.push_back(c);
    }

    std::vector<RWNode *>& getCallers() { return _callers; }
    const std::vector<RWNode *>& getCallers() const { return _callers; }
    */

    const BBlocksVecT &getBBlocks() const { return _bblocks; }

    block_iterator bblocks_begin() { return block_iterator(_bblocks.begin()); }
    block_iterator bblocks_end() { return block_iterator(_bblocks.end()); }

    blocks_range bblocks() { return blocks_range(_bblocks); }

    auto size() const -> decltype(_bblocks.size()) { return _bblocks.size(); }
};

} // namespace dg

#endif // SUBGRAPHBASE_H
