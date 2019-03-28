#ifndef _DG_REACHING_DEFINITIONS_ANALYSIS_H_
#define _DG_REACHING_DEFINITIONS_ANALYSIS_H_

#include <vector>
#include <list>
#include <set>
#include <cassert>
#include <cstring>

#include "dg/analysis/Offset.h"
#include "dg/analysis/SubgraphNode.h"
#include "dg/analysis/BFS.h"
#include "dg/BBlock.h"
#include "dg/ADT/Queue.h"
#include "dg/DGParameters.h"
#include "dg/DependenceGraph.h"

#include "dg/analysis/ReachingDefinitions/ReachingDefinitionsAnalysisOptions.h"
#include "dg/analysis/ReachingDefinitions/DisjunctiveIntervalMap.h"
#include "dg/analysis/ReachingDefinitions/RDMap.h"

// forward declaration
namespace llvm {
    class Value;
}

namespace dg {
namespace analysis {
namespace rd {

namespace srg {
    class AssignmentFinder;
}

class RDNode;
class ReachingDefinitionsAnalysis;

// here the types are for type-checking (optional - user can do it
// when building the graph) and for later optimizations
enum class RDNodeType {
        // invalid type of node
        NONE,
        // these are nodes that just represent memory allocation sites
        // we need to have them even in reaching definitions analysis,
        // so that we can use them as targets in DefSites
        ALLOC,
        DYN_ALLOC,
        // nodes that write the memory
        STORE,
        // nodes that use the memory
        LOAD,
        // merging information from several locations
        PHI,
        // return from the subprocedure
        RETURN,
        // call node
        CALL,
        // return from the call (in caller)
        CALL_RETURN,
        FORK,
        JOIN,
        // dummy nodes
        NOOP
};

extern RDNode *UNKNOWN_MEMORY;

class RDBBlock;

class RDNode : public SubgraphNode<RDNode> {
    RDNodeType type;

    RDBBlock *bblock = nullptr;
    // marks for DFS/BFS
    unsigned int dfsid;
public:

    // for invalid nodes like UNKNOWN_MEMLOC
    RDNode(RDNodeType t = RDNodeType::NONE)
    : SubgraphNode<RDNode>(0), type(t), dfsid(0) {}

    RDNode(unsigned id, RDNodeType t = RDNodeType::NONE)
    : SubgraphNode<RDNode>(id), type(t), dfsid(0) {}

#ifndef NDEBUG
    virtual ~RDNode() = default;
#endif

    // weak update
    DefSiteSetT defs;
    // strong update
    DefSiteSetT overwrites;

    // this is set of variables used in this node
    DefSiteSetT uses;

    // places where this node is defined
    // (so this node has non-empty uses)
    std::vector<RDNode *> defuse;

    RDMap def_map;

    RDNodeType getType() const { return type; }
    DefSiteSetT& getDefines() { return defs; }
    DefSiteSetT& getOverwrites() { return overwrites; }
    DefSiteSetT& getUses() { return uses; }
    const DefSiteSetT& getDefines() const { return defs; }
    const DefSiteSetT& getOverwrites() const { return defs; }
    const DefSiteSetT& getUses() const { return uses; }

    bool defines(RDNode *target, const Offset& off = Offset::UNKNOWN) const
    {
        // FIXME: this is not efficient implementation,
        // use the ordering on the nodes
        // (see old DefMap.h in llvm/)
        if (off.isUnknown()) {
            for (const DefSite& ds : defs)
                if (ds.target == target)
                    return true;
        } else {
            for (const DefSite& ds : defs)
                if (ds.target == target
                    && off.inRange(*ds.offset, *ds.offset + *ds.len))
                    return true;

            for (const DefSite& ds : overwrites)
                if (ds.target == target
                    && off.inRange(*ds.offset, *ds.offset + *ds.len))
                    return true;
        }

        return false;
    }

    /**
     * return true if this node uses UNKNOWN_MEMORY
     */
    bool usesUnknown() const
    {
        bool result = false;
        for (const DefSite& use : uses)
        {
            result |= use.target->isUnknown();
        }
        return result;
    }

    void addUse(RDNode *target, const Offset& off = Offset::UNKNOWN, const Offset& len = Offset::UNKNOWN)
    {
        addUse(DefSite(target, off, len));
    }

    template <typename T>
    void addUse(T&& ds)
    {
        uses.insert(std::forward<T>(ds));
    }

    template <typename T>
    void addUses(T&& u)
    {
        for (auto& ds : u) {
            uses.insert(ds);
        }
    }

    template <typename T>
    void addDefs(T&& defs)
    {
        for (auto& ds : defs) {
            addDef(ds);
        }
    }

    void addDef(const DefSite& ds, bool strong_update = false)
    {
        if (strong_update)
            overwrites.insert(ds);
        else
            defs.insert(ds);

        // TODO: Get rid of this!
        def_map.update(ds, this);
    }

    ///
    // register that the node defines the memory 'target'
    // at offset 'off' of length 'len', i.e. it writes
    // to memory 'target' to bytes [off, off + len].
    void addDef(RDNode *target,
                const Offset& off = Offset::UNKNOWN,
                const Offset& len = Offset::UNKNOWN,
                bool strong_update = false)
    {
        addDef(DefSite(target, off, len), strong_update);
    }

    void addOverwrites(RDNode *target,
                       const Offset& off = Offset::UNKNOWN,
                       const Offset& len = Offset::UNKNOWN)
    {
        addOverwrites(DefSite(target, off, len));
    }

    void addOverwrites(const DefSite& ds)
    {
        overwrites.insert(ds);
    }

    bool isOverwritten(const DefSite& ds)
    {
        return overwrites.find(ds) != overwrites.end();
    }

    const RDMap& getReachingDefinitions() const { return def_map; }
    RDMap& getReachingDefinitions() { return def_map; }
    size_t getReachingDefinitions(RDNode *n, const Offset& off,
                                  const Offset& len, std::set<RDNode *>& ret)
    {
        return def_map.get(n, off, len, ret);
    }

    bool isUnknown() const
    {
        return this == UNKNOWN_MEMORY;
    }

    using KeyType = llvm::Value*;

    // this node is not part of any DependenceGraph
    using DependenceGraphType = DependenceGraph<RDNode>;

    DependenceGraphType *getDG() {
        return nullptr;
    }

    RDBBlock *getBBlock() { return bblock; }
    void setBBlock(RDBBlock *bb) { bblock = bb; }

    friend class ReachingDefinitionsAnalysis;
};

/*
class RDNodeUse : public RDNode {
    // definitions for this use
    std::vector<RDNode *> definitions;

    static RDNodeUse *get(RDNode *n) {
        return n->getType() == RDNodeType::LOAD ?
                static_cast<RDNodeUse *>(n) : nullptr;
    }
};
*/

class RDBBlock {
public:
    using NodeT = RDNode;
    using NodesT = std::list<NodeT *>;

    void append(NodeT *n) { _nodes.push_back(n); n->setBBlock(this); }
    void prepend(NodeT *n) { _nodes.push_front(n); n->setBBlock(this); }
    // FIXME: get rid of this method in favor of either append/prepend
    // (so these method would update CFG edges) or keeping CFG
    // only in blocks
    void prependAndUpdateCFG(NodeT *n) {
        // update CFG edges
        n->insertBefore(_nodes.front());
        // add the node to the block
        _nodes.push_front(n);
        n->setBBlock(this);
    }

    const NodesT& getNodes() const { return _nodes; }

    DefinitionsMap<RDNode> definitions;

    struct edge_iterator {
        // iterator for node edges
        using ItT = decltype(NodeT().getSuccessors().begin());
        ItT it;

        edge_iterator(const ItT& I) : it(I) {}

        bool operator==(const edge_iterator& rhs) const { return it == rhs.it; }
        bool operator!=(const edge_iterator& rhs) const { return it != rhs.it; }
        edge_iterator& operator++() { ++it; return *this; }
        edge_iterator operator++(int) { auto tmp = *this; ++it; return tmp; }
        RDBBlock *operator*() { return (*it)->getBBlock(); }
        RDBBlock *operator->() { return (*it)->getBBlock(); }
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

private:
    NodesT _nodes;
};


class ReachingDefinitionsGraph {
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

    RDNode *create(RDNodeType t) {
      _nodes.emplace_back(new RDNode(++lastNodeID, t));
      return _nodes.back().get();
    }

    void buildBBlocks();
};

class ReachingDefinitionsAnalysis
{
protected:
    ReachingDefinitionsGraph graph;
    unsigned int dfsnum;

    const ReachingDefinitionsAnalysisOptions options;

public:
    ReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph,
                                const ReachingDefinitionsAnalysisOptions& opts)
    : graph(std::move(graph)), dfsnum(0), options(opts)
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

    RDNode *getRoot() const { return graph.getRoot(); }
    ReachingDefinitionsGraph *getGraph() { return &graph; }
    const ReachingDefinitionsGraph *getGraph() const { return &graph; }

    bool processNode(RDNode *n);
    virtual void run();
};

class SSAReachingDefinitionsAnalysis : public ReachingDefinitionsAnalysis {
    std::set<RDNode *> performLvn();
    void performGvn(std::set<RDNode*>&);

    // find definitions of the def site and add def-use edges.
    // For the uncovered bytes create phi nodes and return them.
    std::vector<RDNode *> readValue(RDBBlock *, RDNode *,
                                    const DefSite&, bool addDefUse = true);

public:
    SSAReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph,
                                   const ReachingDefinitionsAnalysisOptions& opts)
    : ReachingDefinitionsAnalysis(std::move(graph), opts) {}

    SSAReachingDefinitionsAnalysis(ReachingDefinitionsGraph&& graph)
    : ReachingDefinitionsAnalysis(std::move(graph)) {}

    void run() override {
        // transform the graph to SSA
        if (graph.getBBlocks().empty())
            graph.buildBBlocks();

        auto phis = performLvn();
        performGvn(phis);
    }
};

} // namespace rd
} // namespace analysis
} // namespace dg

#endif //  _DG_REACHING_DEFINITIONS_ANALYSIS_H_
