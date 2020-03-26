#ifndef DG_RW_NODE_H_
#define DG_RW_NODE_H_

#include <vector>

#include "DefSite.h"
#include "dg/Offset.h"
#include "dg/SubgraphNode.h"

#include "dg/DataDependence/DataDependenceAnalysisOptions.h"

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
        FORK,
        JOIN,
        // dummy nodes
        NOOP
};

extern RWNode *UNKNOWN_MEMORY;

class RWBBlock;

///
/// Gathers information about the node
/// - what memory it accesses and whether it writes it or reads it.
///
struct Annotations {
    // weak update
    DefSiteSetT defs;
    // strong update
    DefSiteSetT overwrites;
    // this is set of variables used in this node
    DefSiteSetT uses;

};

class RWNode : public SubgraphNode<RWNode> {
    RWNodeType type;

    RWBBlock *bblock = nullptr;

    class DefUses {
        using T = std::vector<RWNode *>;
        T defuse;
        // to differentiate between empty() because nothing
        // has been added yet and empty() because there are no
        // definitions
        bool _init{false};

    public:
        bool add(RWNode *d) {
            _init = true;
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
            _init = true;
            bool changed = false;
            for (RWNode *n : C)
                changed |= add(n);
            return changed;
        }

        bool initialized() const { return _init; }

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

    Annotations annotations;

    virtual Annotations& getAnnotations() { return annotations; }
    virtual const Annotations& getAnnotations() const { return annotations; }

    // places where this node is defined
    // (so this node has non-empty uses)
    DefUses defuse;

    RWNodeType getType() const { return type; }

    DefSiteSetT& getDefines() { return getAnnotations().defs; }
    DefSiteSetT& getOverwrites() { return getAnnotations().overwrites; }
    DefSiteSetT& getUses() { return getAnnotations().uses; }
    const DefSiteSetT& getDefines() const { return getAnnotations().defs; }
    const DefSiteSetT& getOverwrites() const { return getAnnotations().overwrites; }
    const DefSiteSetT& getUses() const { return getAnnotations().uses; }

    bool defines(RWNode *target, const Offset& off = Offset::UNKNOWN) const {
        // FIXME: this is not efficient implementation,
        // use the ordering on the nodes
        if (off.isUnknown()) {
            for (const DefSite& ds : getDefines())
                if (ds.target == target)
                    return true;
        } else {
            for (const DefSite& ds : getDefines())
                if (ds.target == target
                    && off.inRange(*ds.offset, *ds.offset + *ds.len))
                    return true;

            for (const DefSite& ds : getOverwrites())
                if (ds.target == target
                    && off.inRange(*ds.offset, *ds.offset + *ds.len))
                    return true;
        }

        return false;
    }

    bool usesUnknown() const {
        for (auto& ds : getUses()) {
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

    void addUse(const DefSite& ds) { getUses().insert(ds); }

    template <typename T>
    void addUses(T&& u) {
        for (auto& ds : u) {
            getUses().insert(ds);
        }
    }

    void addDef(const DefSite& ds, bool strong_update = false) {
        if (strong_update)
            getOverwrites().insert(ds);
        else
            getDefines().insert(ds);
    }

    ///
    // register that the node defines the memory 'target'
    // at offset 'off' of length 'len', i.e. it writes
    // to memory 'target' to bytes [off, off + len].
    void addDef(RWNode *target,
                const Offset& off = Offset::UNKNOWN,
                const Offset& len = Offset::UNKNOWN,
                bool strong_update = false) {
        addDef(DefSite(target, off, len), strong_update);
    }

    template <typename T>
    void addDefs(T&& defs) {
        for (auto& ds : defs) {
            addDef(ds);
        }
    }

    void addOverwrites(RWNode *target,
                       const Offset& off = Offset::UNKNOWN,
                       const Offset& len = Offset::UNKNOWN) {
        addOverwrites(DefSite(target, off, len));
    }

    void addOverwrites(const DefSite& ds) {
        getOverwrites().insert(ds);
    }

    bool isOverwritten(const DefSite& ds) {
        return getOverwrites().find(ds) != getOverwrites().end();
    }

    bool isUnknown() const {
        return this == UNKNOWN_MEMORY;
    }

    bool isUse() const { return !getUses().empty(); }

    const RWBBlock *getBBlock() const { return bblock; }
    RWBBlock *getBBlock() { return bblock; }
    void setBBlock(RWBBlock *bb) { bblock = bb; }

    friend class ReadWriteGraph;
};

// we may either call a properly defined function
// or a function that is undefined and we
// have just a model for it.
class RWCalledValue {
    RWSubgraph *subgraph{nullptr};
    RWNode *calledValue{nullptr};

public:
    RWCalledValue(RWSubgraph *s) : subgraph(s) {}
    RWCalledValue(RWNode *c) : calledValue(c) {}

    bool callsUndefined() const { return calledValue != nullptr; }

    RWSubgraph *getSubgraph() { return subgraph; }
    RWNode *getCalledValue() { return calledValue; }
    const RWSubgraph *getSubgraph() const { return subgraph; }
    const RWNode *getCalledValue() const { return calledValue; }

};

class RWNodeCall : public RWNode {
    // what this call calls?
    using CalleesT = std::vector<RWCalledValue>;
    CalleesT callees;

    //RWNode *callReturn{nullptr};

public:
    RWNodeCall(unsigned id) : RWNode(id, RWNodeType::CALL) {}

    static RWNodeCall *get(RWNode *n) {
        return (n->getType() == RWNodeType::CALL) ?
            static_cast<RWNodeCall*>(n) : nullptr;
    }

    static const RWNodeCall *get(const RWNode *n) {
        return (n->getType() == RWNodeType::CALL) ?
            static_cast<const RWNodeCall*>(n) : nullptr;
    }

    /*
    void setCallReturn(RWNode *callRet) { callReturn = callRet; }
    RWNode *getCallReturn() { return callReturn; }
    const RWNode *getCallReturn() const { return callReturn; }
    */

    bool callsOneUndefined() const {
        if (callees.size() != 1)
            return false;
        return callees[0].callsUndefined();
    }

    RWNode *getSingleUndefined() {
        if (callees.size() != 1)
            return nullptr;
        assert(callees[0].callsUndefined());
        return callees[0].getCalledValue();
    }

    bool callsDefined() const {
        for (auto& c : callees) {
            if (c.getSubgraph()) {
                return true;
            }
        }
        return false;
    }

    const CalleesT& getCallees() const { return callees; }
    CalleesT& getCallees() { return callees; }

    void addCallee(const RWCalledValue& cv) { callees.push_back(cv); }
    void addCallee(RWNode *n) { callees.emplace_back(n); }
    void addCallee(RWSubgraph *s);

#ifndef NDEBUG
    void dump() const;
#endif
};

} // namespace dda
} // namespace dg

#endif //  DG_RW_NODE_H_
