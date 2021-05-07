#ifndef DG_LEGACY_NTSCD_FUNCTION_H
#define DG_LEGACY_NTSCD_FUNCTION_H

#include <iosfwd>
#include <set>

namespace dg {
namespace llvmdg {
namespace legacy {

class Block;

class Function {
  public:
    Function();
    ~Function();

    Block *entry() const;
    Block *exit() const;

    bool addBlock(Block *block);

    std::set<Block *> nodes() const;
    std::set<Block *> condNodes() const;
    std::set<Block *> callReturnNodes() const;

    void dumpBlocks(std::ostream &ostream);
    void dumpEdges(std::ostream &ostream);

  private:
    Block *firstBlock = nullptr;
    Block *lastBlock = nullptr;
    std::set<Block *> blocks;
};

} // namespace legacy
} // namespace llvmdg
} // namespace dg

#endif
