#ifndef FUNCTION_H
#define FUNCTION_H

#include <set>
#include <iosfwd>

namespace dg {
namespace cd {

class Block;

struct Function
{
public:

    Function();
    ~Function();

    Block * entry() const;
    Block * exit() const;

    bool addBlock(Block * block);

    std::set<Block *> condNodes() const;

    void dumpBlocks(std::ostream & ostream);
    void dumpEdges(std::ostream & ostream);

private:

    Block * firstBlock = nullptr;
    Block * lastBlock  = nullptr;
    std::set<Block *> blocks;
};

}
}

#endif // FUNCTION_H
