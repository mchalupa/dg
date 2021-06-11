#include "Function.h"
#include "Block.h"

#include <algorithm>
#include <ostream>

namespace dg {
namespace llvmdg {
namespace legacy {

Function::Function() : lastBlock(new Block(nullptr)) {
    blocks.insert(lastBlock);
}

Function::~Function() {
    for (auto *block : blocks) {
        delete block;
    }
}

Block *Function::entry() const { return firstBlock; }

Block *Function::exit() const { return lastBlock; }

bool Function::addBlock(Block *block) {
    if (!block) {
        return false;
    }
    if (!firstBlock) {
        firstBlock = block;
    }
    return blocks.insert(block).second;
}

std::set<Block *> Function::nodes() const { return blocks; }

std::set<Block *> Function::condNodes() const {
    std::set<Block *> condNodes_;

    std::copy_if(
            blocks.begin(), blocks.end(),
            std::inserter(condNodes_, condNodes_.end()),
            [](const Block *block) { return block->successors().size() > 1; });

    return condNodes_;
}

std::set<Block *> Function::callReturnNodes() const {
    std::set<Block *> callReturnNodes_;

    std::copy_if(blocks.begin(), blocks.end(),
                 std::inserter(callReturnNodes_, callReturnNodes_.end()),
                 [](const Block *block) { return block->isCallReturn(); });

    return callReturnNodes_;
}

void Function::dumpBlocks(std::ostream &ostream) {
    for (auto *block : blocks) {
        block->dumpNode(ostream);
        ostream << "\n";
    }
}

void Function::dumpEdges(std::ostream &ostream) {
    for (auto *block : blocks) {
        block->dumpEdges(ostream);
    }
}

} // namespace legacy
} // namespace llvmdg
} // namespace dg
