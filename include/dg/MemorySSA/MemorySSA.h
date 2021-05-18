#ifndef DG_MEMORY_SSA_H_
#define DG_MEMORY_SSA_H_

#include <cassert>
#include <set>
#include <unordered_map>
#include <vector>

#include "dg/Offset.h"

#include "dg/DataDependence/DataDependenceAnalysisImpl.h"
#include "dg/DataDependence/DataDependenceAnalysisOptions.h"
#include "dg/MemorySSA/DefinitionsMap.h"

#include "dg/ReadWriteGraph/ReadWriteGraph.h"

#include "dg/ADT/Queue.h"
#include "dg/util/debug.h"

#include "Definitions.h"
#include "ModRef.h"

namespace dg {
namespace dda {

class MemorySSATransformation : public DataDependenceAnalysisImpl {
    class BBlockInfo {
        Definitions definitions{};
        RWNodeCall *call{nullptr};

      public:
        void setCallBlock(RWNodeCall *c) { call = c; }
        bool isCallBlock() const { return call != nullptr; }
        RWNodeCall *getCall() { return call; }
        const RWNodeCall *getCall() const { return call; }

        Definitions &getDefinitions() { return definitions; }
        const Definitions &getDefinitions() const { return definitions; }
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
            Summary(Summary &&) = default;
            Summary(const Summary &) = delete;

            void addInput(const DefSite &ds, RWNode *n) { inputs.add(ds, n); }
            void addOutput(const DefSite &ds, RWNode *n) { outputs.add(ds, n); }

            RWNode *getUnknownPhi() {
                // FIXME: optimize this, we create std::set for nothing...
                auto S = inputs.get({UNKNOWN_MEMORY, 0, Offset::UNKNOWN});
                if (S.empty()) {
                    return nullptr;
                }
                assert(S.size() == 1);
                return *(S.begin());
            }

            std::set<RWNode *> getOutputs(const DefSite &ds) {
                return outputs.get(ds);
            }
            auto getUncoveredOutputs(const DefSite &ds) const
                    -> decltype(outputs.undefinedIntervals(ds)) {
                return outputs.undefinedIntervals(ds);
            }
        } summary;

        // sumarized information about visible external
        // effects of the procedure
        ModRefInfo modref;

        SubgraphInfo(RWSubgraph *s);

        friend class MemorySSATransformation;

      public:
        SubgraphInfo() = default;

        Summary &getSummary() { return summary; }
        const Summary &getSummary() const { return summary; }
        BBlockInfo &getBBlockInfo(RWBBlock *b) { return _bblock_infos[b]; }
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
    // XXX: we could avoid this by (at least virtually) splitting blocks on
    // uses.
    static Definitions findDefinitionsInBlock(RWNode *to,
                                              const RWNode *mem = nullptr);
    static Definitions findEscapingDefinitionsInBlock(RWNode *to);
    static void performLvn(Definitions & /*D*/, RWBBlock * /*block*/);
    void updateDefinitions(Definitions &D, RWNode *node);

    ///
    // Find definitions of the def site and return def-use edges.
    // For the uncovered bytes create phi nodes (which are also returned
    // as the definitions).
    std::vector<RWNode *> findDefinitions(RWBBlock * /*block*/,
                                          const DefSite & /*ds*/);
    std::vector<RWNode *> findDefinitions(RWNode *node, const DefSite &ds);

    // Find definitions for the given node (which is supposed to be a use)
    std::vector<RWNode *> findDefinitions(RWNode *node);

    std::vector<RWNode *> findDefinitionsInPredecessors(RWBBlock *block,
                                                        const DefSite &ds);

    void findDefinitionsInMultiplePredecessors(RWBBlock *block,
                                               const DefSite &ds,
                                               std::vector<RWNode *> &defs);

    void addUncoveredFromPredecessors(RWBBlock *block, Definitions &D,
                                      const DefSite &ds,
                                      std::vector<RWNode *> &defs);

    void findPhiDefinitions(RWNode *phi);

    ///
    // Search call C for definitions of ds and store the results into D.
    // Used to implement on-demand search inside procedures.
    void fillDefinitionsFromCall(Definitions &D, RWNodeCall *C,
                                 const DefSite &ds);
    ///
    // Search call C for all definitions that may be visible after the call.
    // After the call to this method, D is completely filled with all
    // information, similarly as when we perform LVN for non-call bblock.
    void fillDefinitionsFromCall(Definitions &D, RWNodeCall *C);

    void findDefinitionsFromCalledFun(RWNode *phi, RWSubgraph *subg,
                                      const DefSite &ds);

    void addDefsFromUndefCall(Definitions &D, RWNode *defs, RWNode *call,
                              bool isstrong);

    template <typename Iterable>
    void findPhiDefinitions(RWNode *phi, const Iterable &I) {
        std::set<RWNode *> defs;

        assert(phi->getOverwrites().size() == 1);
        const auto &ds = *(phi->getOverwrites().begin());
        // we handle this case separately
        assert(!ds.target->isUnknown() && "PHI for unknown memory");

        for (auto *block : I) {
            auto tmpdefs = findDefinitions(block, ds);
            defs.insert(tmpdefs.begin(), tmpdefs.end());
        }

        phi->addDefUse(defs);
    }

    /// Finding definitions for unknown memory
    // Must be called after LVN proceeded - ideally only when the client is
    // getting the definitions
    std::vector<RWNode *> findAllDefinitions(RWNode *from);
    Definitions collectAllDefinitions(RWNode *from);
    /// if escaping is set to true, collect only definitions of escaping memory
    // (optimization for searching definitions in callers)
    void collectAllDefinitions(RWNode *from, Definitions &defs,
                               bool escaping = false);
    void collectAllDefinitions(Definitions &defs, RWBBlock *from,
                               std::set<RWBBlock *> &visitedBlocks,
                               bool escaping);

    void collectAllDefinitionsInCallers(Definitions &defs, RWSubgraph *subg);

    void findDefinitionsInSubgraph(RWNode *phi, RWNodeCall *C,
                                   const DefSite &ds, RWSubgraph *subg);

    void addDefinitionsFromCalledValue(RWNode *phi, RWNodeCall *C,
                                       const DefSite &ds, RWNode *calledValue);

    void computeModRef(RWSubgraph *subg, SubgraphInfo &si);
    bool callMayDefineTarget(RWNodeCall *C, RWNode *target);

    RWNode *createPhi(const DefSite &ds, RWNodeType type = RWNodeType::PHI);
    RWNode *createPhi(Definitions &D, const DefSite &ds,
                      RWNodeType type = RWNodeType::PHI);
    RWNode *createAndPlacePhi(RWBBlock *block, const DefSite &ds);

    // insert a (temporary) use into the graph before the node 'where'
    RWNode *insertUse(RWNode *where, RWNode *mem, const Offset &off,
                      const Offset &len);

    std::vector<RWNode *> _phis;
    dg::ADT::QueueLIFO<RWNode> _queue;
    std::unordered_map<const RWSubgraph *, SubgraphInfo> _subgraphs_info;

    Definitions &getBBlockDefinitions(RWBBlock *b, const DefSite *ds = nullptr);

    SubgraphInfo &getSubgraphInfo(const RWSubgraph *s) {
        return _subgraphs_info[s];
    }
    const SubgraphInfo *getSubgraphInfo(const RWSubgraph *s) const {
        auto it = _subgraphs_info.find(s);
        return it == _subgraphs_info.end() ? nullptr : &it->second;
    }
    BBlockInfo &getBBlockInfo(RWBBlock *b) {
        return getSubgraphInfo(b->getSubgraph()).getBBlockInfo(b);
    }

    const BBlockInfo *getBBlockInfo(RWBBlock *b) const {
        const auto *si = getSubgraphInfo(b->getSubgraph());
        if (si) {
            return si->getBBlockInfo(b);
        }
        return nullptr;
    }

    SubgraphInfo::Summary &getSubgraphSummary(const RWSubgraph *s) {
        return getSubgraphInfo(s).getSummary();
    }

  public:
    MemorySSATransformation(ReadWriteGraph &&graph,
                            const DataDependenceAnalysisOptions &opts)
            : DataDependenceAnalysisImpl(std::move(graph), opts) {}

    MemorySSATransformation(ReadWriteGraph &&graph)
            : DataDependenceAnalysisImpl(std::move(graph)) {}

    void run() override;

    // compute definitions for all uses at once
    // (otherwise the definitions are computed on demand
    // when calling getDefinitions())
    void computeAllDefinitions();

    // return the reaching definitions of ('mem', 'off', 'len')
    // at the location 'where'
    std::vector<RWNode *> getDefinitions(RWNode *where, RWNode *mem,
                                         const Offset &off,
                                         const Offset &len) override;

    std::vector<RWNode *> getDefinitions(RWNode *use) override;

    const Definitions *getDefinitions(RWBBlock *b) const {
        const auto *bi = getBBlockInfo(b);
        return bi ? &bi->getDefinitions() : nullptr;
    }

    const SubgraphInfo::Summary *getSummary(const RWSubgraph *s) const {
        const auto *si = getSubgraphInfo(s);
        if (!si)
            return nullptr;
        return &si->getSummary();
    }
};

} // namespace dda
} // namespace dg

#endif // DG_MEMORY_SSA_H_
