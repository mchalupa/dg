#include "Function.h"
#include "Block.h"

#include <ostream>
namespace dg {
namespace cd {

Function::Function():lastBlock(new Block()) {
    blocks.insert(lastBlock);
}

Function::~Function() {
    for (auto block : blocks) {
        delete block;
    }
}

Block *dg::cd::Function::entry() const {
    return firstBlock;
}

Block *dg::cd::Function::exit() const {
    return lastBlock;
}

bool Function::addBlock(Block *block) {
    if (!block) {
        return false;
    }
    if (!firstBlock) {
        firstBlock = block;
    }
    return blocks.insert(block).second;
}

std::set<Block *> Function::condNodes() const {
    std::set<Block *> condNodes_;
    for (auto block : blocks) {
        const Block * b = block;
        if (b->successors().size() > 1) {
            condNodes_.insert(block);
        }
    }
    return condNodes_;
}

void Function::dumpBlocks(std::ostream &ostream) {
    for (auto block : blocks) {
        block->dumpNode(ostream);
        ostream << "\n";
    }
}

void Function::dumpEdges(std::ostream &ostream) {
    for (auto block : blocks) {
        block->dumpEdges(ostream);
    }
}

}
}
