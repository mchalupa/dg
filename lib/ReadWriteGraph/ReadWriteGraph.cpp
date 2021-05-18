#include <set>
#include <vector>

#include "dg/BBlocksBuilder.h"
#include "dg/ReadWriteGraph/ReadWriteGraph.h"

namespace dg {
namespace dda {

RWNode UNKNOWN_MEMLOC;
RWNode *UNKNOWN_MEMORY = &UNKNOWN_MEMLOC;

#ifndef NDEBUG
void RWNode::dump() const { std::cout << getID() << "\n"; }

void RWNodeCall::dump() const {
    std::cout << getID() << " calls [";
    unsigned n = 0;
    for (const auto &cv : callees) {
        if (n++ > 0) {
            std::cout << ", ";
        }

        if (const auto *subg = cv.getSubgraph()) {
            const auto &nm = subg->getName();
            if (nm.empty()) {
                std::cout << subg;
            } else {
                std::cout << nm;
            }
        } else {
            std::cout << cv.getCalledValue()->getID();
        }
    }
    std::cout << "]\n";
}

void RWBBlock::dump() const {
    std::cout << "bblock " << getID() << "(" << this << ")\n";
    for (auto *n : getNodes()) {
        std::cout << "  ";
        n->dump();
    }
}

#endif

bool RWNode::isDynAlloc() const {
    if (getType() == RWNodeType::DYN_ALLOC)
        return true;

    if (const auto *C = RWNodeCall::get(this)) {
        for (const auto &cv : C->getCallees()) {
            if (const auto *val = cv.getCalledValue()) {
                if (val->getType() == RWNodeType::DYN_ALLOC) {
                    return true;
                }
            }
        }
    }

    return false;
}

void RWNodeCall::addCallee(RWSubgraph *s) {
    callees.emplace_back(s);
    s->addCaller(this);
}

void ReadWriteGraph::removeUselessNodes() {}

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
                      std::vector<std::unique_ptr<RWBBlock>> &newblocks) {
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
            return nullptr;
        }
    }
    return nullptr;
}

void RWSubgraph::splitBBlocksOnCalls() {
    DBG_SECTION_BEGIN(dda, "Splitting basic blocks on calls");
    if (_bblocks.empty()) {
        DBG_SECTION_END(dda, "Splitting basic blocks on calls finished");
        return;
    }

#ifndef NDEBUG
    auto *entry = _bblocks[0].get();
#endif

    std::vector<std::unique_ptr<RWBBlock>> newblocks;

    for (auto &bblock : _bblocks) {
        auto *cur = bblock.get();
        while (cur) {
            cur = splitBlockOnFirstCall(cur, newblocks);
        }
    }

    for (auto &bblock : newblocks) {
        _bblocks.push_back(std::move(bblock));
    }

    assert(entry == _bblocks[0].get() &&
           "splitBBlocksOnCalls() changed the entry");
    DBG_SECTION_END(dda, "Splitting basic blocks on calls finished");
}

} // namespace dda
} // namespace dg
