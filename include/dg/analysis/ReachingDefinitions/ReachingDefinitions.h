#ifndef _DG_REACHING_DEFINITIONS_ANALYSIS_H_
#define _DG_REACHING_DEFINITIONS_ANALYSIS_H_

#include <vector>
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

class RDNode : public SubgraphNode<RDNode> {
    RDNodeType type;

    BBlock<RDNode> *bblock = nullptr;
    // marks for DFS/BFS
    unsigned int dfsid;
public:

    RDNode(RDNodeType t = RDNodeType::NONE)
    : SubgraphNode<RDNode>(0), type(t), dfsid(0) {}

#ifndef NDEBUG
    virtual ~RDNode() = default;
#endif

    // this is the gro of this node, so make it public
    DefSiteSetT defs;
    // this is a subset of defs that are strong update
    // on this node
    DefSiteSetT overwrites;

    // this is set of variables used in this node
    DefSiteSetT uses;

    RDMap def_map;

    RDNodeType getType() const { return type; }
    DefSiteSetT& getDefines() { return defs; }
    DefSiteSetT& getOverwrites() { return overwrites; }
    DefSiteSetT& getUses() { return uses; }
    const DefSiteSetT& getDefines() const { return defs; }
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
        defs.insert(ds);
        def_map.update(ds, this);

        // XXX maybe we could do it by some flag in DefSite?
        // instead of strong new copy... but it should not
        // be big overhead this way... we'll see in the future
        if (strong_update)
            overwrites.insert(ds);
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

    BBlock<RDNode> *getBBlock() {
        return bblock;
    }

    void setBasicBlock(BBlock<RDNode> *bb) {
        bblock = bb;
    }

    void removeCDs() {
    }

    void removeDDs() {
    }

    bool hasSubgraphs() const {
        return false;
    }

    dg::DGParameters<RDNode> *getParameters() const {
        return nullptr;
    }

    std::vector<DependenceGraphType *> getSubgraphs() const {
        return {};
    }

    friend class ReachingDefinitionsAnalysis;
    friend class dg::analysis::rd::srg::AssignmentFinder;
};

class ReachingDefinitionsGraph {
    RDNode *root{nullptr};

public:
    ReachingDefinitionsGraph() = default;
    ReachingDefinitionsGraph(RDNode *r) : root(r) {};
    ReachingDefinitionsGraph(ReachingDefinitionsGraph&&) = default;
    ReachingDefinitionsGraph& operator=(ReachingDefinitionsGraph&&) = default;

    RDNode *getRoot() const { return root; }
    void setRoot(RDNode *r) { root = r; }
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

    bool processNode(RDNode *n);
    virtual void run();
};

} // namespace rd
} // namespace analysis
} // namespace dg

#endif //  _DG_REACHING_DEFINITIONS_ANALYSIS_H_
