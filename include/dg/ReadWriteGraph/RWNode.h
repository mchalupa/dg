#ifndef DG_RW_NODE_H_
#define DG_RW_NODE_H_

#include <vector>

#include "dg/Offset.h"
#include "dg/SubgraphNode.h"

#include "dg/DataDependence/DataDependenceAnalysisOptions.h"
#include "dg/ReachingDefinitions/RDMap.h"

namespace dg {
namespace dda {

class RWNode;
class RWSubgraph;
class ReachingDefinitionsAnalysis;

// here the types are for type-checking (optional - user can do it
// when building the graph) and for later optimizations
enum class RWNodeType {
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
        // artificial use (load)
        MU,
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

extern RWNode *UNKNOWN_MEMORY;

class RWBBlock;

class RWNode : public SubgraphNode<RWNode> {
    RWNodeType type;

    RWBBlock *bblock = nullptr;

    class DefUses {
        using T = std::vector<RWNode *>;
        T defuse;
    public:
        bool add(RWNode *d) {
            for (auto x : defuse) {
                if (x == d) {
                    return false;
                }
            }
            defuse.push_back(d);
            return true;
        }

        template <typename Cont>
        bool add(const Cont& C) {
            bool changed = false;
            for (RWNode *n : C)
                changed |= add(n);
            return changed;
        }

        operator std::vector<RWNode *>() { return defuse; }

        T::iterator begin() { return defuse.begin(); }
        T::iterator end() { return defuse.end(); }
        T::const_iterator begin() const { return defuse.begin(); }
        T::const_iterator end() const { return defuse.end(); }
    };

public:
    // marks for DFS/BFS
    unsigned int dfsid{0};

    // for invalid nodes like UNKNOWN_MEMLOC
    RWNode(RWNodeType t = RWNodeType::NONE)
    : SubgraphNode<RWNode>(0), type(t) {}

    RWNode(unsigned id, RWNodeType t = RWNodeType::NONE)
    : SubgraphNode<RWNode>(id), type(t) {}

#ifndef NDEBUG
    virtual ~RWNode() = default;
    void dump() const;
#endif

    // weak update
    DefSiteSetT defs;
    // strong update
    DefSiteSetT overwrites;

    // this is set of variables used in this node
    DefSiteSetT uses;

    // places where this node is defined
    // (so this node has non-empty uses)
    DefUses defuse;

    // state of the data-flow analysis
    // FIXME: get rid of this in a general node
    RDMap def_map;

    RWNodeType getType() const { return type; }
    DefSiteSetT& getDefines() { return defs; }
    DefSiteSetT& getOverwrites() { return overwrites; }
    DefSiteSetT& getUses() { return uses; }
    const DefSiteSetT& getDefines() const { return defs; }
    const DefSiteSetT& getOverwrites() const { return defs; }
    const DefSiteSetT& getUses() const { return uses; }

    bool defines(RWNode *target, const Offset& off = Offset::UNKNOWN) const
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

    bool usesUnknown() const {
        for (auto& ds : uses) {
            if (ds.target->isUnknown())
                return true;
        }
        return false;
    }

    void addUse(RWNode *target,
                const Offset& off = Offset::UNKNOWN,
                const Offset& len = Offset::UNKNOWN) {
        addUse(DefSite(target, off, len));
    }

    void addUse(const DefSite& ds) { uses.insert(ds); }

    template <typename T>
    void addUses(T&& u)
    {
        for (auto& ds : u) {
            uses.insert(ds);
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
    void addDef(RWNode *target,
                const Offset& off = Offset::UNKNOWN,
                const Offset& len = Offset::UNKNOWN,
                bool strong_update = false)
    {
        addDef(DefSite(target, off, len), strong_update);
    }

    template <typename T>
    void addDefs(T&& defs)
    {
        for (auto& ds : defs) {
            addDef(ds);
        }
    }

    void addOverwrites(RWNode *target,
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

    bool isUnknown() const
    {
        return this == UNKNOWN_MEMORY;
    }

    bool isUse() const { return !uses.empty(); }

    RWBBlock *getBBlock() { return bblock; }
    void setBBlock(RWBBlock *bb) { bblock = bb; }

    friend class ReadWriteGraph;
};

class RWNodeCall : public RWNode {
    // what this call calls?
    std::vector<RWSubgraph *> callees;
    // where it returns?
    RWNode *callReturn{nullptr};

public:
    RWNodeCall(unsigned id) : RWNode(id, RWNodeType::CALL) {}

    static RWNodeCall *get(RWNode *n) {
        return (n->getType() == RWNodeType::CALL) ?
            static_cast<RWNodeCall*>(n) : nullptr;
    }

    void setCallReturn(RWNode *callRet) { callReturn = callRet; }
    RWNode *getCallReturn() { return callReturn; }
    const RWNode *getCallReturn() const { return callReturn; }

    const std::vector<RWSubgraph *>& getCallees() const { return callees; }

    bool addCallee(RWSubgraph *ps) {
        // we suppose there are just few callees,
        // so this should be faster than std::set
        for (auto *p : callees) {
            if (p == ps)
                return false;
        }

        callees.push_back(ps);
        return true;
    }
};

class RWNodeRet : public RWNode {
    // this node returns control to...
    std::vector<RWNode *> returns;

public:
    RWNodeRet(unsigned id)
    :RWNode(id, RWNodeType::RETURN) {}

    static RWNodeRet *get(RWNode *n) {
        return n->getType() == RWNodeType::RETURN ?
            static_cast<RWNodeRet *>(n) : nullptr;
    }

    const std::vector<RWNode*>& getReturnSites() const { return returns; }

    bool addReturnSite(RWNode *r) {
        // we suppose there are just few callees,
        // so this should be faster than std::set
        for (RWNode *p : returns) {
            if (p == r)
                return false;
        }

        returns.push_back(r);
        return true;
    }
};



} // namespace dda
} // namespace dg

#endif //  DG_RW_NODE_H_
