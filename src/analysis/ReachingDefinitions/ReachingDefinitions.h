#ifndef _DG_REACHING_DEFINITIONS_ANALYSIS_H_
#define _DG_REACHING_DEFINITIONS_ANALYSIS_H_

#include <vector>
#include <set>
#include <cassert>
#include <cstring>

#include "analysis/SubgraphNode.h"
#include "analysis/SCC.h"
#include "analysis/PointsTo/PointerSubgraph.h"
#include "analysis/Offset.h"

#include "ADT/Queue.h"
#include "RDMap.h"

namespace dg {
namespace analysis {
namespace rd {

class RDNode;
class ReachingDefinitionsAnalysis;

// here the types are for type-checking (optional - user can do it
// when building the graph) and for later optimizations
enum RDNodeType {
        // for backward compatibility
        NONE = 0,
        // these are nodes that just represent memory allocation sites
        // we need to have them even in reaching definitions analysis,
        // so that we can use them as targets in DefSites
        ALLOC = 1,
        STORE,
        DYN_ALLOC,
        PHI,
        // return from the subprocedure
        RETURN,
        // call node
        CALL,
        // return from the call (in caller)
        CALL_RETURN,
        // dummy nodes
        NOOP
};

extern RDNode *UNKNOWN_MEMORY;

class RDNode : public SubgraphNode<RDNode> {
    RDNodeType type;

    // marks for DFS/BFS
    unsigned int dfsid;

#ifdef DEBUG_ENABLED
    // same data as in PSNode
    const char *name;
#endif

public:
    RDNode(RDNodeType t = NONE)
        : type(t), dfsid(0),
#ifdef DEBUG_ENABLED
          name(nullptr)
#endif
    {}

    // this is the gro of this node, so make it public
    DefSiteSetT defs;
    // this is a subset of defs that are strong update
    // on this node
    DefSiteSetT overwrites;

    RDMap def_map;

    RDNodeType getType() const { return type; }

#ifdef DEBUG_ENABLED
    const char *getName() const { return name; }
    void setName(const char *n) { delete name; name = strdup(n); }
#endif

    DefSiteSetT& getDefines() { return defs; }
    const DefSiteSetT& getDefines() const { return defs; }

    bool defines(RDNode *target, const Offset& off = UNKNOWN_OFFSET) const
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

    void addDef(RDNode *target,
                const Offset& off = UNKNOWN_OFFSET,
                const Offset& len = UNKNOWN_OFFSET,
                bool strong_update = false)
    {
        addDef(DefSite(target, off, len), strong_update);
    }

    void addOverwrites(RDNode *target,
                       const Offset& off = UNKNOWN_OFFSET,
                       const Offset& len = UNKNOWN_OFFSET)
    {
        addOverwrites(DefSite(target, off, len));
    }

    void addOverwrites(const DefSite& ds)
    {
        overwrites.insert(ds);
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


    friend class ReachingDefinitionsAnalysis;
};

class ReachingDefinitionsAnalysis
{
    RDNode *root;
    unsigned int dfsnum;
    bool field_insensitive;
    uint32_t max_set_size;

    ADT::QueueFIFO<RDNode *> queue;

    // strongly connected components of the RDSubgraph
    std::vector<std::vector<RDNode *> > SCCs;
    SCCCondensation<RDNode> SCCcond;
    std::set<unsigned, std::greater<unsigned>> scc_to_queue;

public:
    ReachingDefinitionsAnalysis(RDNode *r,
                                bool field_insens = false,
                                uint32_t max_set_sz = ~((uint32_t)0))
    : root(r), dfsnum(0), field_insensitive(field_insens), max_set_size(max_set_sz)
    {
        assert(r && "Root cannot be null");
        // with max_set_size == 0 (everything is defined on unknown location)
        // we get unsound results with vararg functions and similar weird stuff
        assert(max_set_size > 0 && "The set size must be at least 1");

        // compute the strongly connected components

        SCC<RDNode> scc_comp;
        // use move semantics instead of copying,
        // since the scc_comp object goes from the scope anyway
        SCCs = std::move(scc_comp.compute(root));

        // compute condensation graph from SCC
        SCCcond.compute(SCCs);
    }

    void getNodes(std::set<RDNode *>& cont)
    {
        assert(root && "Do not have root");

        ++dfsnum;

        ADT::QueueLIFO<RDNode *> lifo;
        lifo.push(root);
        root->dfsid = dfsnum;

        while (!lifo.empty()) {
            RDNode *cur = lifo.pop();
            cont.insert(cur);

            for (RDNode *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    lifo.push(succ);
                }
            }
        }
    }

    RDNode *getRoot() const { return root; }
    void setRoot(RDNode *r) { root = r; }

    bool processNode(RDNode *n);

    void _addSCCSuccessors(unsigned idx)
    {
        for (auto succ_idx : SCCcond[idx].getSuccessors()) {
            scc_to_queue.insert(succ_idx);
            // the condensation is a dag, so we must stop eventually
            // in this recursion
            _addSCCSuccessors(succ_idx);
        }
    }

    void queueNodesInReverseTopoOrder()
    {
        auto tmp = scc_to_queue;
        for (unsigned idx : tmp)
            _addSCCSuccessors(idx);

        // OK, now we have what we need in scc_to_queue
        // (the scc that we should add and also
        // its successors)
        for (unsigned idx : scc_to_queue) {
            for (auto node : SCCs[idx])
                queue.push(node);
        }

        scc_to_queue.clear();
    }

    void enqueue(RDNode *n)
    {
        scc_to_queue.insert(n->getSCCId());
    }

    void run()
    {
        assert(root && "Do not have root");

        enqueue(root);
        queueNodesInReverseTopoOrder();

        while (!queue.empty()) {
            RDNode *cur = queue.pop();

            if (processNode(cur))
                enqueue(cur);

            if (queue.empty())
                queueNodesInReverseTopoOrder();
        }

        assert(scc_to_queue.empty());
    }

};

} // namespace rd
} // namespace analysis
} // namespace dg

#endif //  _DG_REACHING_DEFINITIONS_ANALYSIS_H_
