#ifndef _DG_REACHING_DEFINITIONS_ANALYSIS_H_
#define _DG_REACHING_DEFINITIONS_ANALYSIS_H_

#include <vector>
#include <list>
#include <set>
#include <cassert>
#include <memory>

#include "dg/analysis/Offset.h"
#include "dg/analysis/BFS.h"

#include "dg/analysis/ReachingDefinitions/ReachingDefinitionsAnalysisOptions.h"
#include "dg/analysis/ReachingDefinitions/RDMap.h"
#include "dg/analysis/ReachingDefinitions/DefinitionsMap.h"

#include "dg/analysis/ReachingDefinitions/RDNode.h"

#include "dg/util/debug.h"

namespace dg {
namespace analysis {
namespace rd {

// here the types are for type-checking (optional - user can do it
// when building the graph) and for later optimizations

class RDBBlock {
    void _check() {
#ifndef NDEBUG
        // first node can have several predecessors
        // and the last node can have several successors,
        // otherwise the structure must be a chain
        if (_nodes.size() <= 1)
            return;

        auto it = _nodes.begin();
        assert((*it)->getSuccessors().size() == 1);
        do {
            ++it;
            if (it == _nodes.end())
                return;
            assert((*it)->getPredecessors().size() == 1);
            assert((*it)->getSuccessors().size() == 1 || ++it == _nodes.end());
        } while (it != _nodes.end());
#endif // not NDEBUG
    }

public:
    using NodeT = RDNode;
    using NodeSuccIterator = decltype(NodeT().getSuccessors().begin());
    using NodesT = std::list<NodeT *>;

    void append(NodeT *n) { _nodes.push_back(n); n->setBBlock(this); _check(); }
    void prepend(NodeT *n) { _nodes.push_front(n); n->setBBlock(this); _check(); }
    // FIXME: get rid of this method in favor of either append/prepend
    // (so these method would update CFG edges) or keeping CFG
    // only in blocks
    void prependAndUpdateCFG(NodeT *n) {
        // precondition for this method,
        // we can fix it at some point
        assert(!_nodes.empty());

        assert(n->getSuccessors().empty());
        assert(n->getPredecessors().empty());

        // update CFG edges
        n->insertBefore(_nodes.front());

        prepend(n);
        assert(!n->getSuccessors().empty());
        assert(n->getBBlock() == this);
        assert(n->getSingleSuccessor()->getBBlock() == this);

        _check();
    }

    const NodesT& getNodes() const { return _nodes; }

    DefinitionsMap<RDNode> definitions;

    // override the operator* method in the successor/predecessor iterator of the node
    struct edge_iterator : public NodeSuccIterator {
        edge_iterator() = default;
        edge_iterator(const NodeSuccIterator& I) : NodeSuccIterator(I) {}

        RDBBlock *operator*() { return NodeSuccIterator::operator*()->getBBlock(); }
        RDBBlock *operator->() { return NodeSuccIterator::operator*()->getBBlock(); }
    };

    edge_iterator pred_begin() { return edge_iterator(_nodes.front()->getPredecessors().begin()); }
    edge_iterator pred_end() { return edge_iterator(_nodes.front()->getPredecessors().end()); }
    edge_iterator succ_begin() { return edge_iterator(_nodes.back()->getSuccessors().begin()); }
    edge_iterator succ_end() { return edge_iterator(_nodes.back()->getSuccessors().end()); }

    RDBBlock *getSinglePredecessor() {
        auto& preds = _nodes.front()->getPredecessors();
        return preds.size() == 1 ? (*preds.begin())->getBBlock() : nullptr;
    }

    RDBBlock *getSingleSuccessor() {
        auto& succs = _nodes.back()->getSuccessors();
        return succs.size() == 1 ? (*succs.begin())->getBBlock() : nullptr;
    }

    NodeT *getFirst() { return _nodes.empty() ? nullptr : _nodes.front(); }
    NodeT *getLast() { return _nodes.empty() ? nullptr : _nodes.back(); }

private:
    NodesT _nodes;
};


class ReachingDefinitionsGraph {
    // FIXME: get rid of this
    unsigned int dfsnum{1};

    size_t lastNodeID{0};
    RDNode *root{nullptr};
    using BBlocksVecT = std::vector<std::unique_ptr<RDBBlock>>;
    using NodesT = std::vector<std::unique_ptr<RDNode>>;

    // iterator over the bblocks that returns the bblock,
    // not the unique_ptr to the bblock
    struct block_iterator : public BBlocksVecT::iterator {
        using ContainedType
            = std::remove_reference<decltype(*(std::declval<BBlocksVecT::iterator>()->get()))>::type;

        block_iterator(const BBlocksVecT::iterator& it) : BBlocksVecT::iterator(it) {}

        ContainedType *operator*() {
            return (BBlocksVecT::iterator::operator*()).get();
        };
        ContainedType *operator->() {
            return ((BBlocksVecT::iterator::operator*()).get());
        };
    };

    BBlocksVecT _bblocks;

    struct blocks_range {
        BBlocksVecT& blocks;
        blocks_range(BBlocksVecT& b) : blocks(b) {}

        block_iterator begin() { return block_iterator(blocks.begin()); }
        block_iterator end() { return block_iterator(blocks.end()); }
    };

    NodesT _nodes;

public:
    ReachingDefinitionsGraph() = default;
    ReachingDefinitionsGraph(RDNode *r) : root(r) {};
    ReachingDefinitionsGraph(ReachingDefinitionsGraph&&) = default;
    ReachingDefinitionsGraph& operator=(ReachingDefinitionsGraph&&) = default;

    RDNode *getRoot() const { return root; }
    void setRoot(RDNode *r) { root = r; }

    const std::vector<std::unique_ptr<RDBBlock>>& getBBlocks() const { return _bblocks; }

    block_iterator blocks_begin() { return block_iterator(_bblocks.begin()); }
    block_iterator blocks_end() { return block_iterator(_bblocks.end()); }

    blocks_range blocks() { return blocks_range(_bblocks); }

    void removeUselessNodes();

    void optimize() {
        removeUselessNodes();
    }

    RDNode *create(RDNodeType t) {
      _nodes.emplace_back(new RDNode(++lastNodeID, t));
      return _nodes.back().get();
    }

    // Build blocks for the nodes. If 'dce' is set to true,
    // the dead code is eliminated after building the blocks.
    void buildBBlocks(bool dce = false);

    // get nodes in BFS order and store them into
    // the container
    template <typename ContainerOrNode>
    std::vector<RDNode *> getNodes(const ContainerOrNode& start,
                                   unsigned expected_num = 0)
    {
        ++dfsnum;

        std::vector<RDNode *> cont;
        if (expected_num != 0)
            cont.reserve(expected_num);

        struct DfsIdTracker {
            const unsigned dfsnum;
            DfsIdTracker(unsigned dnum) : dfsnum(dnum) {}

            void visit(RDNode *n) { n->dfsid = dfsnum; }
            bool visited(RDNode *n) const { return n->dfsid == dfsnum; }
        };

        DfsIdTracker visitTracker(dfsnum);
        BFS<RDNode, DfsIdTracker> bfs(visitTracker);

        bfs.run(start,
                [&cont](RDNode *n) {
                    cont.push_back(n);
                });

        return cont;
    }
};

class ReachingDefinitionsAnalysis
{
protected:
    ReachingDefinitionsGraph graph;

    const ReachingDefinitionsAnalysisOptions options;

public:
    ReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph,
                                const ReachingDefinitionsAnalysisOptions& opts)
    : graph(std::move(graph)), options(opts)
    {
        assert(graph.getRoot() && "Root cannot be null");
        // with max_set_size == 0 (everything is defined on unknown location)
        // we get unsound results with vararg functions and similar weird stuff
        assert(options.maxSetSize > 0 && "The set size must be at least 1");
    }

    ReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph)
    : ReachingDefinitionsAnalysis(std::move(graph), {}) {}
    virtual ~ReachingDefinitionsAnalysis() = default;

    // get nodes in BFS order and store them into
    // the container
    template <typename ContainerOrNode>
    std::vector<RDNode *> getNodes(const ContainerOrNode& start,
                                   unsigned expected_num = 0) {
        return graph.getNodes(start, expected_num);
    }

    RDNode *getRoot() const { return graph.getRoot(); }
    ReachingDefinitionsGraph *getGraph() { return &graph; }
    const ReachingDefinitionsGraph *getGraph() const { return &graph; }

    bool processNode(RDNode *n);
    virtual void run();

    // return the reaching definitions of ('mem', 'off', 'len')
    // at the location 'where'
    virtual std::vector<RDNode *>
    getReachingDefinitions(RDNode *where, RDNode *mem,
                           const Offset& off,
                           const Offset& len);

    // return reaching definitions of a node that represents
    // the given use
    virtual std::vector<RDNode *> getReachingDefinitions(RDNode *use);
};

class SSAReachingDefinitionsAnalysis : public ReachingDefinitionsAnalysis {
    void performLvn();
    void performLvn(RDBBlock *block);
    void performGvn();

    ////
    // LVN
    ///

    // Find definitions of the def site and return def-use edges.
    // For the (possibly) uncovered bytes create phi nodes (which are also returned
    // as the definitions) in _this very block_. It is important for LVN.
    std::vector<RDNode *> findDefinitionsInBlock(RDBBlock *, const DefSite&);

    ////
    // GVN
    ///
    // Find definitions of the def site and return def-use edges.
    // For the uncovered bytes create phi nodes (which are also returned
    // as the definitions).
    std::vector<RDNode *> findDefinitions(RDBBlock *, const DefSite&);

    /// Finding definitions for unknown memory
    // Must be called after LVN proceeded - ideally only when the client is getting the definitions
    std::vector<RDNode *> findAllReachingDefinitions(RDNode *from);
    void findAllReachingDefinitions(DefinitionsMap<RDNode>& defs, RDBBlock *from,
                                    std::set<RDNode *>& nodes,
                                    std::set<RDBBlock *>& visitedBlocks);

    // all phi nodes added during transformation to SSA
    std::vector<RDNode *> _phis;

public:
    SSAReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph,
                                   const ReachingDefinitionsAnalysisOptions& opts)
    : ReachingDefinitionsAnalysis(std::move(graph), opts) {}

    SSAReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph)
    : ReachingDefinitionsAnalysis(std::move(graph)) {}

    void run() override {
        DBG_SECTION_BEGIN(dda, "Running MemorySSA analysis");
        // transform the graph to SSA
        if (graph.getBBlocks().empty())
            graph.buildBBlocks();

        performLvn();
        performGvn();

        DBG_SECTION_END(dda, "Running MemorySSA analysis finished");
    }

    // return the reaching definitions of ('mem', 'off', 'len')
    // at the location 'where'
    std::vector<RDNode *>
    getReachingDefinitions(RDNode *, RDNode *,
                           const Offset&,
                           const Offset&) override {
        assert(false && "This method is not implemented for this analysis");
        abort();
    }

    std::vector<RDNode *> getReachingDefinitions(RDNode *use) override;
};

} // namespace rd
} // namespace analysis
} // namespace dg

#endif //  _DG_REACHING_DEFINITIONS_ANALYSIS_H_
