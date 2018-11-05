#ifndef _DG_BBLOCKS_BUILDER_H_
#define _DG_BBLOCKS_BUILDER_H_

#include <cassert>
#include "dg/ADT/Queue.h"

namespace dg {
namespace analysis {

///
// Generate basic blocks from nodes with successors.
template <typename BBlockT>
class BBlocksBuilder {
    using NodeT = typename BBlockT::NodeT;

    std::vector<std::unique_ptr<BBlockT>> _blocks;

    // FIXME: use bitvector?
    std::set<unsigned> _processed;
    ADT::QueueFIFO<NodeT *> _queue;

    bool enqueue(NodeT *n) {
        if (_processed.insert(n->getID()).second == false)
            return false; // we already queued this node

        _queue.push(n);
        assert(enqueue(n) == false);
        return true;
    }

    void setNewBlock(NodeT *cur) {
        auto blk = new BBlockT();
        _blocks.emplace_back(blk);
        blk->append(cur);
        cur->setBBlock(blk);
    }

    void addToBlock(NodeT *cur, BBlockT *blk) {
        cur->setBBlock(blk);
        blk->append(cur);
    }

    void setBlock(NodeT *cur) {
        if (cur->predecessorsNum() == 0 // root node
            || cur->predecessorsNum() > 1) { // join
            setNewBlock(cur);
            return;
        }

        assert(cur->predecessorsNum() == 1);
        // if we are the entry node after branching,
        // we create a new block
        if (cur->getSinglePredecessor()->successorsNum() > 1) {
            setNewBlock(cur);
            return;
        }

        // We are inside a block, set the block from
        // the predecessor
        assert(cur->getSinglePredecessor()->getBBlock());
        addToBlock(cur, cur->getSinglePredecessor()->getBBlock());
    }

public:
    void buildBlocks(NodeT *root) {
        enqueue(root);

        while (!_queue.empty()) {
            NodeT *cur = _queue.pop();
            assert(cur->getBBlock() == nullptr);

            setBlock(cur);

            // queue successors for processing
            for (NodeT *succ : cur->getSuccessors()) {
                enqueue(succ);
            }
        }
    }

    std::vector<std::unique_ptr<BBlockT>>& getBlocks() { return _blocks; }

    std::vector<std::unique_ptr<BBlockT>>&&
    buildAndGetBlocks(NodeT *root) {
        buildBlocks(root);
        return _blocks;
    }
};


} // namespace analysis
} // namespace dg

#endif // _DG_BBLOCKS_BUILDER_H_
