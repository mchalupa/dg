#ifndef DG_MEMORY_SSA_H_
#define DG_MEMORY_SSA_H_

#include <vector>
#include <set>
#include <cassert>
#include <unordered_map>

#include "dg/Offset.h"

#include "dg/DataDependence/DataDependenceAnalysisOptions.h"
#include "dg/DataDependence/DataDependenceAnalysisImpl.h"
#include "dg/MemorySSA/DefinitionsMap.h"

#include "dg/ReadWriteGraph/ReadWriteGraph.h"

#include "dg/ADT/Queue.h"
#include "dg/util/debug.h"

namespace dg {
namespace dda {

class MemorySSATransformation : public DataDependenceAnalysisImpl {

    // information about definitions associated to each bblock
    struct Definitions {
        bool _processed{false};

        // definitions gathered at the end of this bblock
        // (if you find the sought memory here,
        // you got all definitions from this block)
        DefinitionsMap<RWNode> definitions;
        // all memory that is overwritten by this block (strong update)
        // FIXME: we should have just a mapping from memory to disjunctive intervals
        // as data structure here (if you find the sought memory here, you can
        // terminate the search)
        DefinitionsMap<RWNode> kills;

        // writes to unknown memory in this block
        std::vector<RWNode*> unknownWrites;
        // just a cache
        std::vector<RWNode*> unknownReads;

        void addUnknownWrite(RWNode *n) {
            unknownWrites.push_back(n);
        }

        void addUnknownRead(RWNode *n) {
            unknownReads.push_back(n);
        }

        const std::vector<RWNode *>& getUnknownWrites() const {
            return unknownWrites;
        }

        const std::vector<RWNode *>& getUnknownReads() const {
            return unknownReads;
        }

        // update this Definitions by definitions from 'node'.
        // I.e., as if node would be executed when already
        // having the definitions we have
        void update(RWNode *node, RWNode *defnode = nullptr);
        // Join another definitions to this Definitions
        // (as if on  a joint point in a CFG)
        void join(const Definitions& rhs);

        auto uncovered(const DefSite& ds) const -> decltype(kills.undefinedIntervals(ds)) {
            return kills.undefinedIntervals(ds);
        }

        // for on-demand analysis
        // once isProcessed is true, the Defiitions contain
        // summarized all the information that one needs
        // (may be then altered just only by adding a phi node definitions)
        bool isProcessed() const { return _processed; }
        void setProcessed() { _processed = true; }

#ifndef NDEBUG
        void dump() const;
#endif
    };

    class BBlockInfo {
        Definitions definitions{};
        RWNodeCall *call{nullptr};

    public:
        void setCallBlock(RWNodeCall *c) { call = c; }
        bool isCallBlock() const { return call != nullptr; }
        RWNodeCall *getCall() { return call; }
        const RWNodeCall *getCall() const { return call; }

        Definitions& getDefinitions() { return definitions; }
        const Definitions& getDefinitions() const { return definitions; }
    };

    class SubgraphInfo {
        std::unordered_map<RWBBlock *, BBlockInfo> _bblock_infos;

        class Summary {
        public:
            // phi nodes representing reads/writes to memory that is
            // external to the procedure
            DefinitionsMap<RWNode> inputs;
            DefinitionsMap<RWNode> outputs;

            Summary() = default;
            Summary(Summary&&) = default;
            Summary(const Summary&) = delete;

            void addInput(const DefSite& ds, RWNode *n) { inputs.add(ds, n); }
            void addOutput(const DefSite& ds, RWNode *n) { outputs.add(ds, n); }

            std::set<RWNode *> getOutputs(const DefSite& ds) { return outputs.get(ds); }
            auto getUncoveredOutputs(const DefSite& ds) -> decltype (outputs.undefinedIntervals(ds)) {
                return outputs.undefinedIntervals(ds);
            }
        } summary;

        // sumarized information about visible external
        // effects of the procedure
        class ModRefInfo {
            // to distinguish between empty and non-computed modref information
            bool _initialized{false};
        public:
            // the set of memory that is defined in this procedure
            // and is external to the subgraph or is local but its address is taken
            // In other words, memory whose definitions can be "visible"
            // outside the procedure.
            // FIXME: we should keep only sets of DefSites
            DefinitionsMap<RWNode> maydef;
            // external or local address-taken memory that can be
            // used inside the procedure
            // FIXME: we should keep only sets of DefSites
            DefinitionsMap<RWNode> mayref;
            // memory that must be defined in this procedure
            // (on every path through the procedure)
            DefinitionsMap<RWNode> mustdef;

            void addMayDef(const DefSite& ds, RWNode *def) {
                // FIXME: do not store def, it is useless. Just takes memory...
                maydef.add(ds, def);
            }

            template <typename C>
            void addMayDef(const C& c, RWNode *def) {
                for (auto& ds : c) {
                        maydef.add(ds, def);
                }
            }

            void addMayRef(const DefSite& ds, RWNode *ref) {
                // FIXME: do not store ref, it is useless. Just takes memory...
                mayref.add(ds, ref);
            }

            template <typename C>
            void addMayRef(const C& c, RWNode *ref) {
                for (auto& ds : c) {
                        mayref.add(ds, ref);
                }
            }

            void addMustDef(const DefSite& ds, RWNode *def) {
                // FIXME: do not store def, it is useless. Just takes memory...
                mustdef.add(ds, def);
            }

            template <typename C>
            void addMustDef(const C& c, RWNode *def) {
                for (auto& ds : c) {
                        mustdef.add(ds, def);
                }
            }

            void add(const ModRefInfo& oth) {
                maydef.add(oth.maydef);
                mayref.add(oth.mayref);
                mustdef.add(oth.mustdef);
            }

            void setInitialized() { _initialized = true; }
            bool isInitialized() const { return _initialized  ; }
        } modref;

        SubgraphInfo(RWSubgraph *s);

        friend class MemorySSATransformation;

    public:
        SubgraphInfo() = default;

        Summary& getSummary() { return summary; }
        const Summary& getSummary() const { return summary; }
        BBlockInfo& getBBlockInfo(RWBBlock *b) { return _bblock_infos[b]; }
        const BBlockInfo *getBBlockInfo(RWBBlock *b) const {
            auto it = _bblock_infos.find(b);
            return it == _bblock_infos.end() ? nullptr : &it->second;
        }
    };

    void initialize();

    ////
    // LVN
    ///
    // Perform LVN up to a certain point and search only for a certain memory.
    // XXX: we could avoid this by (at least virtually) splitting blocks on uses.
    Definitions findDefinitionsInBlock(RWNode *to, const RWNode *mem = nullptr);
    void performLvn(Definitions&, RWBBlock *);
    void updateDefinitions(Definitions& D, RWNode *node);

    ///
    // Find definitions of the def site and return def-use edges.
    // For the uncovered bytes create phi nodes (which are also returned
    // as the definitions).
    std::vector<RWNode *> findDefinitions(RWBBlock *, const DefSite&);

    // Find definitions for the given node (which is supposed to be a use)
    std::vector<RWNode *> findDefinitions(RWNode *node);

    std::vector<RWNode *> findDefinitionsInPredecessors(RWBBlock *block,
                                                        const DefSite& ds);

        void findDefinitionsInMultiplePredecessors(RWBBlock *block,
                                                   const DefSite& ds,
                                                   std::vector<RWNode *>& defs);

    void addUncoveredFromPredecessors(RWBBlock *block,
                                      MemorySSATransformation::Definitions& D,
                                      const DefSite& ds,
                                      std::vector<RWNode *>& defs);

    void findPhiDefinitions(RWNode *phi);

    void findDefinitionsFromCall(Definitions& D, RWNodeCall *C, const DefSite& ds);
    void findDefinitionsFromCalledFun(RWNode *phi, RWSubgraph *subg, const DefSite& ds);

    void addDefsFromUndefCall(Definitions& D, RWNode *defs,
                              RWNode *call, bool isstrong);

    template <typename Iterable>
    void findPhiDefinitions(RWNode *phi, Iterable& I) {
        std::set<RWNode *> defs;

        assert(phi->getOverwrites().size() == 1);
        const auto& ds = *(phi->getOverwrites().begin());
        // we handle this case separately
        assert(!ds.target->isUnknown() && "PHI for unknown memory");

        for (auto *block : I) {
            auto tmpdefs = findDefinitions(block, ds);
            defs.insert(tmpdefs.begin(), tmpdefs.end());
        }

        phi->addDefUse(defs);
    }

    /// Finding definitions for unknown memory
    // Must be called after LVN proceeded - ideally only when the client is getting the definitions
    std::vector<RWNode *> findAllDefinitions(RWNode *from);
    DefinitionsMap<RWNode> collectAllDefinitions(RWNode *from);
    void collectAllDefinitions(DefinitionsMap<RWNode>& defs,
                               RWBBlock *from,
                               std::set<RWBBlock *>& visitedBlocks);

    void findAllDefinitionsFromCall(Definitions& D, RWNodeCall *C);

    void computeModRef(RWSubgraph *subg, SubgraphInfo& si);

    RWNode *createPhi(const DefSite& ds);
    RWNode *createPhi(Definitions& D, const DefSite& ds);
    RWNode *createAndPlacePhi(RWBBlock *block, const DefSite& ds);

    // insert a (temporary) use into the graph before the node 'where'
    RWNode *insertUse(RWNode *where, RWNode *mem,
                      const Offset& off, const Offset& len);

    std::vector<RWNode *> _phis;
    dg::ADT::QueueLIFO<RWNode> _queue;
    std::unordered_map<RWBBlock *, DefinitionsMap<RWNode>> _cached_defs;
    std::unordered_map<const RWSubgraph *, SubgraphInfo> _subgraphs_info;

    Definitions& getBBlockDefinitions(RWBBlock *b, const DefSite *ds = nullptr);
    DefinitionsMap<RWNode>& getCachedDefinitions(RWBBlock *b);
    bool hasCachedDefinitions(RWBBlock *b) const { return _cached_defs.count(b) > 0; }

    SubgraphInfo& getSubgraphInfo(const RWSubgraph *s) { return _subgraphs_info[s]; }
    const SubgraphInfo *getSubgraphInfo(const RWSubgraph *s) const {
        auto it = _subgraphs_info.find(s);
        return it == _subgraphs_info.end() ? nullptr : &it->second;
    }
    BBlockInfo& getBBlockInfo(RWBBlock *b) {
        return getSubgraphInfo(b->getSubgraph()).getBBlockInfo(b);
    }

    const BBlockInfo *getBBlockInfo(RWBBlock *b) const {
        auto *si = getSubgraphInfo(b->getSubgraph());
        if (si) {
            return si->getBBlockInfo(b);
        }
        return nullptr;
    }

    SubgraphInfo::Summary& getSubgraphSummary(const RWSubgraph *s) {
        return getSubgraphInfo(s).getSummary();
    }

public:
    MemorySSATransformation(ReadWriteGraph&& graph,
                            const DataDependenceAnalysisOptions& opts)
    : DataDependenceAnalysisImpl(std::move(graph), opts) {}

    MemorySSATransformation(ReadWriteGraph&& graph)
    : DataDependenceAnalysisImpl(std::move(graph)) {}

    void run() override;

    // compute definitions for all uses at once
    // (otherwise the definitions are computed on demand
    // when calling getDefinitions())
    void computeAllDefinitions();

    // return the reaching definitions of ('mem', 'off', 'len')
    // at the location 'where'
    std::vector<RWNode *> getDefinitions(RWNode *where,
                                         RWNode *mem,
                                         const Offset& off,
                                         const Offset& len) override;

    std::vector<RWNode *> getDefinitions(RWNode *use) override;

    const Definitions *getDefinitions(RWBBlock *b) const {
        auto *bi = getBBlockInfo(b);
        return bi ? &bi->getDefinitions() : nullptr;
    }

    const SubgraphInfo::Summary *getSummary(const RWSubgraph *s) const {
        auto si = getSubgraphInfo(s);
        if (!si)
            return nullptr;
        return &si->getSummary();
    }
};

} // namespace dda
} // namespace dg

#endif // DG_MEMORY_SSA_H_
