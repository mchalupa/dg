#ifndef DG_GRAPHBUILDER_H_
#define DG_GRAPHBUILDER_H_

#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_os_ostream.h>

#include "dg/ADT/SetQueue.h"
#include "dg/llvm/CallGraph/CallGraph.h"

namespace dg {

template <typename NodeT>
class NodesSeq {
    // we can optimize this later...
    std::vector<NodeT *> nodes;
    NodeT *representant{nullptr};

  public:
    NodesSeq(const std::initializer_list<NodeT *> &lst) {
        if (lst.size() > 0) {
            nodes.insert(nodes.end(), lst.begin(), lst.end());
            representant = *lst.begin();
        }
    }

    NodesSeq(NodesSeq &&) = default;
    NodesSeq(const NodesSeq &) = default;

    NodeT *setRepresentant(NodeT *r) { representant = r; }

    NodeT *getRepresentant() const { return representant; }

    bool empty() const { return nodes.empty(); }

    auto begin() -> decltype(nodes.begin()) { return nodes.begin(); }
    auto end() -> decltype(nodes.end()) { return nodes.end(); }
    auto begin() const -> decltype(nodes.begin()) { return nodes.begin(); }
    auto end() const -> decltype(nodes.end()) { return nodes.end(); }
};

template <typename NodeT, typename BBlockT, typename SubgraphT>
class GraphBuilder {
    struct SubgraphInfo {
        using BlocksMappingT =
                std::unordered_map<const llvm::BasicBlock *, BBlockT *>;

        SubgraphT &subgraph;
        BlocksMappingT blocks{};

        SubgraphInfo(SubgraphT &s) : subgraph(s) {}
        SubgraphInfo(SubgraphInfo &&) = default;
        SubgraphInfo(const SubgraphInfo &) = delete;
    };

    using GlobalsT = std::vector<NodeT *>;
    using NodesMappingT =
            std::unordered_map<const llvm::Value *, NodesSeq<NodeT>>;
    using ValuesMappingT =
            std::unordered_map<const NodeT *, const llvm::Value *>;
    using SubgraphsMappingT =
            std::unordered_map<const llvm::Function *, SubgraphInfo>;

    const llvm::Module *_module;

    SubgraphsMappingT _subgraphs;
    NodesMappingT _nodes;
    ValuesMappingT _nodeToValue;
    GlobalsT _globals;

    void buildCFG(SubgraphInfo &subginfo) {
        for (auto &it : subginfo.blocks) {
            auto llvmblk = it.first;
            auto *bblock = it.second;

            for (auto *succ : successors(llvmblk)) {
                auto succit = subginfo.blocks.find(succ);
                assert((succit != subginfo.blocks.end()) &&
                       "Do not have the block built");

                bblock->addSuccessor(succit->second);
            }
        }
    }

    void buildGlobals() {
        DBG_SECTION_BEGIN(dg, "Building globals");

        for (const auto &G : _module->globals()) {
            // every global node is like memory allocation
            auto cur = buildNode(&G);
            _globals.insert(_globals.end(), cur.begin(), cur.end());
        }

        DBG_SECTION_END(dg, "Building globals done");
    }

  protected:
    NodesSeq<NodeT> buildNode(const llvm::Value *val) {
        auto it = _nodes.find(val);
        if (it != _nodes.end()) {
            return it->second;
        }

        const auto &nds = createNode(val);
        assert((nds.getRepresentant() || nds.empty()) &&
               "Built node sequence has no representant");

        if (auto *repr = nds.getRepresentant()) {
            _nodes.emplace(val, nds);

            assert((_nodeToValue.find(repr) == _nodeToValue.end()) &&
                   "Mapping a node that we already have");
            _nodeToValue[repr] = val;
        }

        return nds;
    }

    BBlockT &buildBBlock(const llvm::BasicBlock &B, SubgraphInfo &subginfo) {
        auto &bblock = createBBlock(&B, subginfo.subgraph);
        assert(subginfo.blocks.find(&B) == subginfo.blocks.end() &&
               "Already have this basic block");
        subginfo.blocks[&B] = &bblock;

        for (const auto &I : B) {
            for (auto *node : buildNode(&I)) {
                bblock.append(node);
            }
        }

        return bblock;
    }

    void buildSubgraph(const llvm::Function &F) {
        using namespace llvm;

        DBG_SECTION_BEGIN(dg,
                          "Building the subgraph for " << F.getName().str());
        auto subgit = _subgraphs.find(&F);
        assert(subgit != _subgraphs.end() && "Do not have that subgraph");

        auto &subginfo = subgit->second;

        DBG(dg, "Building basic blocks of " << F.getName().str());
        // do a walk through basic blocks such that all predecessors of
        // a block are searched before the block itself
        // (operands must be created before their use)
        ADT::SetQueue<ADT::QueueFIFO<const llvm::BasicBlock *>> queue;
        const auto &entry = F.getEntryBlock();
        queue.push(&entry);

        while (!queue.empty()) {
            const auto *cur = queue.pop();

            buildBBlock(*cur, subginfo);

            for (const auto *succ : successors(cur)) {
                queue.push(succ);
            }
        }

        DBG(dg, "Building CFG");
        buildCFG(subginfo);

        DBG_SECTION_END(dg, "Building the subgraph done");
    }

    void buildAllFuns() {
        DBG(dg, "Building all functions from LLVM module");
        for (const auto &F : *_module) {
            if (F.isDeclaration()) {
                continue;
            }
            assert(_subgraphs.find(&F) == _subgraphs.end() &&
                   "Already have that subgraph");
            auto &subg = createSubgraph(&F);
            _subgraphs.emplace(&F, subg);
        }

        // now do the real thing
        for (const auto &F : *_module) {
            if (!F.isDeclaration()) {
                buildSubgraph(F);
            }
        }
    }

    void buildFunsFromCG(llvmdg::CallGraph *cg) {
        const auto &funs = cg->functions();
        // we should have at least the entry fun
        assert(!funs.empty() && "No function in call graph");

        for (const auto *F : funs) {
            DBG(dg, "Building functions based on call graph information");
            assert(_subgraphs.find(F) == _subgraphs.end() &&
                   "Already have that subgraph");
            auto &subg = createSubgraph(F);
            _subgraphs.emplace(F, subg);
        }

        // now do the real thing
        for (const auto *F : funs) {
            if (!F->isDeclaration()) {
                buildSubgraph(*F);
            }
        }
    }

  public:
    GraphBuilder(const llvm::Module *m) : _module(m) {}
    virtual ~GraphBuilder() = default;

    const llvm::Module *getModule() const { return _module; }
    const llvm::DataLayout *getDataLayout() const {
        return &_module->getDataLayout();
    }

    const GlobalsT &getGlobals() const { return _globals; }

    const NodesMappingT &getNodesMapping() const { return _nodes; }

    const ValuesMappingT &getValuesMapping() const { return _nodeToValue; }

    const SubgraphsMappingT &getSubgraphsMapping() const { return _subgraphs; }

    NodeT *getNode(const llvm::Value *v) {
        auto it = _nodes.find(v);
        return it == _nodes.end() ? nullptr : it->second.getRepresentant();
    }

    const NodeT *getNode(const llvm::Value *v) const {
        auto it = _nodes.find(v);
        return it == _nodes.end() ? nullptr : it->second.getRepresentant();
    }

    const llvm::Value *getValue(const NodeT *n) const {
        auto it = _nodeToValue.find(n);
        return it == _nodeToValue.end() ? nullptr : it->second;
    }

    SubgraphT *getSubgraph(const llvm::Function *f) {
        auto it = _subgraphs.find(f);
        return it == _subgraphs.end() ? nullptr : &it->second.subgraph;
    }

    const SubgraphT *getSubgraph(const llvm::Function *f) const {
        auto it = _subgraphs.find(f);
        return it == _subgraphs.end() ? nullptr : &it->second.subgraph;
    }

    virtual NodesSeq<NodeT> createNode(const llvm::Value *) = 0;
    virtual BBlockT &createBBlock(const llvm::BasicBlock *, SubgraphT &) = 0;
    virtual SubgraphT &createSubgraph(const llvm::Function *) = 0;

    void buildFromLLVM(llvmdg::CallGraph *cg = nullptr) {
        assert(_module && "Do not have the LLVM module");

        buildGlobals();

        // create emtpy subgraphs for each procedure,
        // so that calls can use them as operands

        if (cg) {
            buildFunsFromCG(cg);
        } else {
            buildAllFuns();
        }
    }
};

} // namespace dg

#endif
