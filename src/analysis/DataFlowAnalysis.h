#ifndef _DG_DATA_FLOW_ANALYSIS_H_
#define _DG_DATA_FLOW_ANALYSIS_H_

#include <utility>
#include <set>

#include "Analysis.h"
#include "DFS.h"

#ifndef ENABLE_CFG
#error "Need CFG enabled for data flow analysis"
#endif

namespace dg {
namespace analysis {

// ordering of nodes with respect to DFS order
// works for both nodes and blocks
template<typename T>
struct DFSOrderLess
{
    bool operator()(T a, T b) const
    {
        return a->getDFSOrder() < b->getDFSOrder();
    }
};

template <typename NodePtrT>
class BBlockDataFlowAnalysis : public Analysis<NodePtrT>
{
public:
    BBlockDataFlowAnalysis<NodePtrT>(BBlock<NodePtrT> *entryBB)
        :entryBB(entryBB)
    {}

    virtual bool runOnBlock(BBlock<NodePtrT> *BB) = 0;

    void run()
    {
        bool changed = false;
        assert(entryBB && "entry basic block is nullptr");
        BlocksSetT blocks;

        BBlockDFS<NodePtrT> DFS;
        DFSDataT data(blocks, changed, this);

        // we will get all the nodes using DFS
        DFS.run(entryBB, dfs_proc_bb, data);

        // Iterate over the nodes in dfs reverse order, it is
        // usually good for reaching fixpoint. Later we can
        // add options what ordering to use.
        // Since we used while loop, if nothing changed after the
        // first iteration (the DFS), the loop will never run
        while (changed) {
            changed = false;
            for (auto I = blocks.rbegin(), E = blocks.rend();
                 I != E; ++I) {
                changed |= runOnBlock(*I);
            }
        }
    }

private:
    // define set of blocks to be ordered in dfs order
    typedef std::set<BBlock<NodePtrT> *,
                     DFSOrderLess<BBlock<NodePtrT> *>> BlocksSetT;
    struct DFSDataT
    {
        DFSDataT(BlocksSetT& b, bool& c, BBlockDataFlowAnalysis<NodePtrT> *r)
            :blocks(b), changed(c), ref(r) {}

        BlocksSetT& blocks;
        bool& changed;
        BBlockDataFlowAnalysis<NodePtrT> *ref;
    };

    static void dfs_proc_bb(BBlock<NodePtrT> *BB,
                            DFSDataT& data)
    {
        data.changed |= data.ref->runOnBlock(BB);
        data.blocks.insert(BB);
    }

    BBlock<NodePtrT> *entryBB;
};


template <typename NodePtrT>
class DataFlowAnalysis : public BBlockDataFlowAnalysis<NodePtrT>
{
public:
    DataFlowAnalysis<NodePtrT>(BBlock<NodePtrT> *entryBB)
        : BBlockDataFlowAnalysis<NodePtrT>(entryBB) {};

    /* virtual */
    bool runOnBlock(BBlock<NodePtrT> *B)
    {
        bool changed = false;
        NodePtrT n = B->getFirstNode();

        while(n) {
            changed |= runOnNode(n);
            n = n->getSuccessor();
        }

        return changed;
    }

    virtual bool runOnNode(NodePtrT n) = 0;

private:
};

} // namespace analysis
} // namespace dg

#endif //  _DG_DATA_FLOW_ANALYSIS_H_
