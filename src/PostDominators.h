
#ifndef _DG_POST_DOMINATORS_
#define _DG_POST_DOMINATORS_

#include <set>

#include "Utils.h"
#include "BBlock.h"

namespace dg {

template <typename BBlockT>
class PDTreeBuilder
{
public:
    PDTreeBuilder<BBlockT>(BBlockT *exitBlock)
        : exitBlock(exitBlock)
    {
        assert(exitBlock && "given nullptr exitBlock");
    }

    bool build()
    {
        // only data-flow algorithm yet
        return PDTreeDataFlow();
    }

private:

    // very unefficent data-flow algorithm to compute
    // post-dominators tree
    bool PDTreeDataFlow()
    {
        if (!computePDSets())
            return false;

        // compute immediate pdominators
        // XXX

        // compute numbers using DFS
        // XXX

        return true;
    }

    // put all BBlock into queue in reverse order
    size_t getBlocks(std::set<BBlockT *>& blocks)
    {
        size_t inserted = 0;
        std::queue<BBlockT *> toProcess;
        toProcess.push(exitBlock);

        while (!toProcess.empty()) {
            BBlockT *block = toProcess.front();
            toProcess.pop();

            // put block into queue
            // if it already is there, do not add its childs,
            // so that we won't be looping indefinitely
            if (!blocks.insert(block).second)
                continue;
            else
                ++inserted;

            // take all predcessors of the block and put them into
            // queue for processing. When there are no predcessors
            // nothing is put into queue, thus the whole process
            // will stop one day
            for (BBlockT *b : block->predcessors()) {
                toProcess.push(b);
            }
        }

        return inserted;
    }

    bool processBlocks(std::set<BBlockT *>& blocks)
    {
        bool changed = false;
        for (BBlockT *block : blocks) {
            auto &pdoms = block->getPostdominates();
            typename BBlockT::ContainerT tmp;

            for (BBlockT *succ : block->successors()) {
                auto succpd = succ->getPostdominates();
                // uninitialized sets act as universal sets,
                // so we will skip them
                if (succpd.empty())
                    continue;

                // if this is first non-empty set, assign it to
                // tmp so that we have something to intersect with
                if (tmp.empty())
                    tmp = succpd;
                else
                    // if we have something, then we can
                    // intersect it
                    tmp.intersect(succpd);
            }

            // union with this block
            tmp.insert(block);

            if (tmp == pdoms)
                changed = false;
            else {
                pdoms.swap(tmp);
                changed = true;
            }
        }

        return changed;
    }

    // this is very unefficient, but working
    bool computePDSets()
    {
        bool changed;
        std::set<BBlockT *> blocks;

        // get all basic blocks
        getBlocks(blocks);

        // do fixpoint
        do {
            changed = processBlocks(blocks);
        } while (changed);

        return true;
    }

    BBlockT *exitBlock;
};

} // namespace dg

#endif // _DG_POST_DOMINATORS_
