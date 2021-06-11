#ifndef DG_RWBBLOCK_H_
#define DG_RWBBLOCK_H_

#include <memory>

#include "dg/BBlockBase.h"
#include "dg/ReadWriteGraph/RWNode.h"

namespace dg {
namespace dda {

class RWBBlock;
class RWNode;

class RWBBlock : public BBlockBase<RWBBlock, RWNode> {
    RWSubgraph *subgraph{nullptr};

  public:
    using NodeT = RWNode;

    RWBBlock() = default;
    RWBBlock(RWSubgraph *s) : subgraph(s) {}

    RWSubgraph *getSubgraph() { return subgraph; }
    const RWSubgraph *getSubgraph() const { return subgraph; }

    /*
    auto begin() -> decltype(_nodes.begin()) { return _nodes.begin(); }
    auto begin() const -> decltype(_nodes.begin()) { return _nodes.begin(); }
    auto end() -> decltype(_nodes.end()) { return _nodes.end(); }
    auto end() const -> decltype(_nodes.end()) { return _nodes.end(); }
    */

    // Split the block before and after the given node.
    // Return newly created basic blocks (there are at most two of them).
    std::pair<std::unique_ptr<RWBBlock>, std::unique_ptr<RWBBlock>>
    splitAround(NodeT *node) {
        assert(node->getBBlock() == this && "Spliting a block on invalid node");

        RWBBlock *withnode = nullptr;
        RWBBlock *after = nullptr;

        if (getNodes().size() == 1) {
            assert(*getNodes().begin() == node);
            return {nullptr, nullptr};
        }

#ifndef NDEBUG
        auto old_size = getNodes().size();
        assert(old_size > 1);
#endif
        unsigned num = 0;
        auto it = getNodes().begin(), et = getNodes().end();
        for (; it != et; ++it) {
            if (*it == node) {
                break;
            }
            ++num;
        }

        assert(it != et && "Did not find the node");
        assert(*it == node);

        ++it;
        if (it != et) {
            after = new RWBBlock(subgraph);
            for (; it != et; ++it) {
                after->append(*it);
            }
        }

        // truncate nodes in this block
        if (num > 0) {
            withnode = new RWBBlock(subgraph);
            withnode->append(node);

            getNodes().resize(num);
        } else {
            assert(*getNodes().begin() == node);
            assert(after && "Should have a suffix");
            getNodes().resize(1);
        }

        assert(!withnode || withnode->size() == 1);
        assert(((getNodes().size() + (withnode ? withnode->size() : 0) +
                 (after ? after->size() : 0)) == old_size) &&
               "Bug in splitting nodes");

        // reconnect edges
        RWBBlock *bbwithsuccessors = after;
        if (!bbwithsuccessors) // no suffix
            bbwithsuccessors = withnode;

        assert(bbwithsuccessors);
        for (auto *s : this->_successors) {
            for (auto &p : s->_predecessors) {
                if (p == this) {
                    p = bbwithsuccessors;
                }
            }
        }
        // swap this and after successors
        bbwithsuccessors->_successors.swap(this->_successors);

        if (withnode) {
            this->addSuccessor(withnode);
            if (after) {
                withnode->addSuccessor(after);
            }
        } else {
            assert(after && "Should have a suffix");
            this->addSuccessor(after);
        }

        return {std::unique_ptr<RWBBlock>(withnode),
                std::unique_ptr<RWBBlock>(after)};
    }

    bool isReturnBBlock() const {
        if (const auto *last = getLast()) {
            return last->isRet();
        }
        return false;
    }

#ifndef NDEBUG
    void dump() const;
#endif
};

} // namespace dda
} // namespace dg

#endif // DG_RWBBLOCK_H_
