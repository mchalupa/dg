#ifndef DG_RW_NODE_H_
#define DG_RW_NODE_H_

#include <vector>

#include "DefSite.h"
#include "dg/Offset.h"
#include "dg/SubgraphNode.h"
#include "dg/util/iterators.h"

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
    GLOBAL,
    // nodes that write the memory
    STORE,
    // nodes that use the memory
    LOAD,
    // merging information from several locations
    PHI,
    ////  PHIs used to pass information between procedures
    // PHIs on the side of procedure (formal arguments)
    INARG,
    OUTARG,
    // PHIs on the side of call (actual arguments)
    CALLIN,
    CALLOUT,
    // artificial use (load)
    MU,
    // return from the subprocedure
    RETURN,
    // call node
    CALL,
    FORK,
    JOIN,
    // node that may define/use memory but does
    // not fall into any of these categories
    // (e.g., it represents an undefined call for which we have a model)
    GENERIC,
    // dummy nodes
    NOOP
};

class RWBBlock;

class RWNode : public SubgraphNode<RWNode> {
    RWNodeType type;
    bool has_address_taken{false};
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
            for (auto *x : defuse) {
                if (x == d) {
                    return false;
                }
            }
            defuse.push_back(d);
            return true;
        }

        template <typename Cont>
        bool add(const Cont &C) {
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
    ///
    /// Gathers information about the node
    /// - what memory it accesses and whether it writes it or reads it.
    ///
    mutable struct Annotations {
        // weak update
        DefSiteSetT defs;
        // strong update
        DefSiteSetT overwrites;
        // this is set of variables used in this node
        DefSiteSetT uses;

        DefSiteSetT &getDefines() { return defs; }
        DefSiteSetT &getOverwrites() { return overwrites; }
        DefSiteSetT &getUses() { return uses; }
        const DefSiteSetT &getDefines() const { return defs; }
        const DefSiteSetT &getOverwrites() const { return overwrites; }
        const DefSiteSetT &getUses() const { return uses; }
    } annotations;

    // for invalid nodes like UNKNOWN_MEMLOC
    RWNode(RWNodeType t = RWNodeType::NONE)
            : SubgraphNode<RWNode>(0), type(t) {}

    RWNode(unsigned id, RWNodeType t = RWNodeType::NONE)
            : SubgraphNode<RWNode>(id), type(t) {}

    RWNodeType getType() const { return type; }

    // FIXME: create a child class (RWNodeAddressTaken??)
    // and move this bool there, it is not relevant for all nodes
    // (from this node then can inherit alloca, etc.)
    bool hasAddressTaken() const { return has_address_taken; }
    void setAddressTaken() { has_address_taken = true; }

    virtual ~RWNode() = default;

#ifndef NDEBUG
    void dump() const override;
#endif

    // places where this node is defined
    // (so this node has non-empty uses)
    // FIXME: add a getter
    DefUses defuse;

    bool addDefUse(RWNode *n) { return defuse.add(n); }

    template <typename C>
    bool addDefUse(const C &c) {
        return defuse.add(c);
    }

    virtual Annotations &getAnnotations() { return annotations; }
    virtual const Annotations &getAnnotations() const { return annotations; }

    DefSiteSetT &getDefines() { return getAnnotations().getDefines(); }
    DefSiteSetT &getOverwrites() { return getAnnotations().getOverwrites(); }
    DefSiteSetT &getUses() { return getAnnotations().getUses(); }
    const DefSiteSetT &getDefines() const {
        return getAnnotations().getDefines();
    }
    const DefSiteSetT &getOverwrites() const {
        return getAnnotations().getOverwrites();
    }
    const DefSiteSetT &getUses() const { return getAnnotations().getUses(); }

    bool defines(const RWNode *target,
                 const Offset &off = Offset::UNKNOWN) const {
        // FIXME: this is not efficient implementation,
        // use the ordering on the nodes
        if (off.isUnknown()) {
            for (const DefSite &ds : getDefines())
                if (ds.target == target)
                    return true;
            for (const DefSite &ds : getOverwrites())
                if (ds.target == target)
                    return true;
        } else {
            for (const DefSite &ds : getDefines())
                if (ds.target == target &&
                    off.inRange(*ds.offset, *ds.offset + *ds.len))
                    return true;

            for (const DefSite &ds : getOverwrites())
                if (ds.target == target &&
                    off.inRange(*ds.offset, *ds.offset + *ds.len))
                    return true;
        }

        return false;
    }

    bool usesUnknown() const {
        return dg::any_of(getUses(), [](const DefSite &ds) {
            return ds.target->isUnknown();
        });
    }

    bool usesOnlyGlobals() const {
        return !dg::any_of(getUses(), [](const DefSite &ds) {
            return !ds.target->isGlobal();
        });
    }

    // add uses to annotations of 'this' object
    // (call objects can have several annotations as they are
    //  composed of several nodes)
    void addUse(const DefSite &ds) { annotations.getUses().insert(ds); }

    void addUse(RWNode *target, const Offset &off = Offset::UNKNOWN,
                const Offset &len = Offset::UNKNOWN) {
        addUse(DefSite(target, off, len));
    }

    template <typename T>
    void addUses(T &&u) {
        for (auto &ds : u) {
            annotations.getUses().insert(ds);
        }
    }

    // add definitions to annotations of 'this' object
    // (call objects can have several annotations as they are
    //  composed of several nodes)
    void addDef(const DefSite &ds, bool strong_update = false) {
        if (strong_update)
            annotations.getOverwrites().insert(ds);
        else
            annotations.getDefines().insert(ds);
    }

    ///
    // register that the node defines the memory 'target'
    // at offset 'off' of length 'len', i.e. it writes
    // to memory 'target' to bytes [off, off + len].
    void addDef(RWNode *target, const Offset &off = Offset::UNKNOWN,
                const Offset &len = Offset::UNKNOWN,
                bool strong_update = false) {
        addDef(DefSite(target, off, len), strong_update);
    }

    template <typename T>
    void addDefs(T &&defs) {
        for (auto &ds : defs) {
            addDef(ds);
        }
    }

    void addOverwrites(RWNode *target, const Offset &off = Offset::UNKNOWN,
                       const Offset &len = Offset::UNKNOWN) {
        addOverwrites(DefSite(target, off, len));
    }

    void addOverwrites(const DefSite &ds) {
        annotations.getOverwrites().insert(ds);
    }

    bool isUnknown() const { return this == UNKNOWN_MEMORY; }
    bool isUse() const { return !getUses().empty(); }
    bool isDef() const {
        return !getDefines().empty() || !getOverwrites().empty();
    }

    bool isInOut() const {
        return getType() == RWNodeType::INARG ||
               getType() == RWNodeType::OUTARG ||
               getType() == RWNodeType::CALLIN ||
               getType() == RWNodeType::CALLOUT;
    }
    bool isPhi() const { return getType() == RWNodeType::PHI || isInOut(); }
    bool isGlobal() const { return getType() == RWNodeType::GLOBAL; }
    bool isCall() const { return getType() == RWNodeType::CALL; }
    bool isAlloc() const { return getType() == RWNodeType::ALLOC; }
    bool isAllocation() const { return isAlloc() || isDynAlloc(); }
    bool isRet() const { return getType() == RWNodeType::RETURN; }
    bool isDynAlloc() const;

    bool canEscape() const {
        return isDynAlloc() || isGlobal() || hasAddressTaken();
    }

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
    // PHI nodes representing defined/used memory
    // by the call
    using InputsT = std::vector<RWNode *>;
    using OutputsT = std::vector<RWNode *>;
    RWNode *unknownInput{nullptr};

    CalleesT callees;
    InputsT inputs;
    OutputsT outputs;

    mutable bool _annotations_summarized{false};

    // compute the overall effect of all undefined calls
    // in this call and store them into the annotations
    // of this node
    void _summarizeAnnotation() const {
        if (callees.size() > 1) {
            std::vector<const RWNode *> undefined;
            for (const auto &cv : callees) {
                if (const auto *uc = cv.getCalledValue()) {
                    undefined.push_back(uc);
                }
            }

            if (undefined.size() == 1) {
                annotations = undefined[0]->annotations;
            } else if (undefined.size() > 1) {
                auto kills = undefined[0]->annotations.overwrites.intersect(
                        undefined[1]->annotations.overwrites);
                for (size_t i = 2; i < undefined.size(); ++i) {
                    kills = kills.intersect(
                            undefined[i]->annotations.overwrites);
                }

                annotations.overwrites = kills;
                for (const auto *u : undefined) {
                    annotations.defs.add(u->annotations.defs);
                    annotations.uses.add(u->annotations.uses);
                }
            }
        }
        _annotations_summarized = true;
    }

  public:
    RWNodeCall(unsigned id) : RWNode(id, RWNodeType::CALL) {}

    static RWNodeCall *get(RWNode *n) {
        return n->isCall() ? static_cast<RWNodeCall *>(n) : nullptr;
    }

    static const RWNodeCall *get(const RWNode *n) {
        return n->isCall() ? static_cast<const RWNodeCall *>(n) : nullptr;
    }

    RWNode *getUnknownPhi() { return unknownInput; }

    RWCalledValue *getSingleCallee() {
        if (callees.size() != 1)
            return nullptr;
        return &callees[0];
    }

    const RWCalledValue *getSingleCallee() const {
        if (callees.size() != 1)
            return nullptr;
        return &callees[0];
    }

    RWNode *getSingleUndefined() {
        auto *cv = getSingleCallee();
        return cv ? cv->getCalledValue() : nullptr;
    }

    const RWNode *getSingleUndefined() const {
        const auto *cv = getSingleCallee();
        return cv ? cv->getCalledValue() : nullptr;
    }

    bool callsOneUndefined() const { return getSingleUndefined() != nullptr; }

    bool callsDefined() const {
        return dg::any_of(callees, [](const RWCalledValue &c) {
            return c.getSubgraph() != nullptr;
        });
    }

    bool callsUndefined() const {
        return dg::any_of(callees, [](const RWCalledValue &c) {
            return c.getCalledValue() != nullptr;
        });
    }

    const CalleesT &getCallees() const { return callees; }
    CalleesT &getCallees() { return callees; }

    void addCallee(const RWCalledValue &cv) { callees.push_back(cv); }
    void addCallee(RWNode *n) { callees.emplace_back(n); }
    void addCallee(RWSubgraph *s);

    Annotations &getAnnotations() override {
        if (auto *uc = getSingleUndefined())
            return uc->annotations;
        if (!_annotations_summarized)
            _summarizeAnnotation();
        return annotations;
    }
    const Annotations &getAnnotations() const override {
        if (const auto *uc = getSingleUndefined())
            return uc->annotations;
        if (!_annotations_summarized)
            _summarizeAnnotation();
        return annotations;
    }

    void addOutput(RWNode *n) {
        assert(n->isPhi());
        outputs.push_back(n);
    }
    const OutputsT &getOutputs() const { return outputs; }

    void addInput(RWNode *n) {
        assert(n->isPhi());
        inputs.push_back(n);
    }

    void addUnknownInput(RWNode *n) {
        assert(unknownInput == nullptr);
        assert(n->isPhi());
        inputs.push_back(n);
        unknownInput = n;
    }
    const InputsT &getInputs() const { return inputs; }

#ifndef NDEBUG
    void dump() const override;
#endif
};

} // namespace dda
} // namespace dg

#endif //  DG_RW_NODE_H_
