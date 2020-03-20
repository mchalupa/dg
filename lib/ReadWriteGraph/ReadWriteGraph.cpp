#include <set>
#include <vector>

#include "dg/ReachingDefinitions/ReachingDefinitions.h"
#include "dg/BBlocksBuilder.h"

namespace dg {
namespace dda {

void ReadWriteGraph::removeUselessNodes() {
}

void RWSubgraph::buildBBlocks(bool /*dce*/) {
    assert(getRoot() && "No root node");
    DBG(dda, "Building basic blocks");

    BBlocksBuilder<RWBBlock> builder;
    _bblocks = std::move(builder.buildAndGetBlocks(getRoot()));

    assert(getRoot()->getBBlock() && "Root node has no BBlock");

    // should we eliminate dead code?
    // The dead code are the nodes that have no basic block assigned
    // (follows from the DFS nature of the block builder algorithm)
    /*
    if (!dce)
        return;

    for (auto& nd : _nodes) {
        if (nd->getBBlock() == nullptr) {
            nd->isolate();
            nd.reset();
        }
    }
    */
}

// split the block on the first call and return the
// block containing the rest of the instructions
// (or nullptr if there's nothing else to do)
static RWBBlock *
splitBlockOnFirstCall(RWBBlock *block,
                      std::vector<std::unique_ptr<RWBBlock>>& newblocks) {
    for (auto *node : block->getNodes()) {
        if (auto *call = RWNodeCall::get(node)) {
            if (call->callsOneUndefined()) {
                // ignore calls that call one udefined function,
                // those behave just like usual read/write
                continue;
            }
            DBG(dda, "Splitting basic block around " << node->getID());
            auto blks = block->splitAround(node);
            if (blks.first)
                newblocks.push_back(std::move(blks.first));
            if (blks.second) {
                newblocks.push_back(std::move(blks.second));
                return newblocks.back().get();
            }
        }
    }
    return nullptr;
}

void RWSubgraph::splitBBlocksOnCalls() {
    DBG_SECTION_BEGIN(dda, "Splitting basic blocks on calls");
    if (_bblocks.size() == 0)
        return;

#ifndef NDEBUG
    auto *entry = _bblocks[0].get();
#endif

    std::vector<std::unique_ptr<RWBBlock>> newblocks;

    for (auto& bblock : _bblocks) {
        auto *cur = bblock.get();
        while(cur) {
            cur = splitBlockOnFirstCall(cur, newblocks);
        }
    }

    for (auto& bblock : newblocks) {
        _bblocks.push_back(std::move(bblock));
    }

    assert(entry == _bblocks[0].get()
            && "splitBBlocksOnCalls() changed the entry");
    DBG_SECTION_END(dda, "Splitting basic blocks on calls finished");
}

} // namespace dda
} // namespace dg
