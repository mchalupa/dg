#ifndef LLVM_DG_RWG_BUILDER_H
#define LLVM_DG_RWG_BUILDER_H

#include <unordered_map>
#include <set>
#include <memory>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/CFG.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/ReadWriteGraph/ReadWriteGraph.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/DataDependence/LLVMDataDependenceAnalysisOptions.h"

#ifndef NDEBUG
#include "dg/util/debug.h"
#endif // NDEBUG

namespace dg {
namespace dda {

template <typename NodeT>
class NodesSeq {
    // we can optimize this later...
    std::vector<NodeT*> nodes;
    NodeT *representant{nullptr};

public:
    NodesSeq(const std::initializer_list<NodeT*>& lst) {
        if (lst.size() > 0) {
            nodes.insert(nodes.end(), lst.begin(), lst.end());
            representant = *lst.begin();
        }
    }

    NodeT *setRepresentant(NodeT *r) {
        representant = r;
    }

    NodeT *getRepresentant() const {
        return representant;
    }

    auto begin() -> decltype(nodes.begin()) { return nodes.begin(); }
    auto end() -> decltype(nodes.end()) { return nodes.end(); }
    auto begin() const -> decltype(nodes.begin()) { return nodes.begin(); }
    auto end() const -> decltype(nodes.end()) { return nodes.end(); }
};
 

template <typename NodeT, typename BBlockT, typename SubgraphT>
class GraphBuilder {
    using NodesMappingT = std::unordered_map<const llvm::Value *, NodeT *>;
    using SubgraphsMappingT = std::unordered_map<const llvm::Value *, SubgraphT *>;

    const llvm::Module *_module;

    /*
    struct Subgraph {
        SubgraphT *subgraph;
        Subgraph(const SubgraphT&) = delete;
    };
    */

    SubgraphsMappingT _subgraphs;
    NodesMappingT _nodes;

    void buildCFG(const llvm::Function& F, SubgraphT& subg) {
    }

    void buildICFG() {
        DBG_SECTION_BEGIN(rwg, "Building call edges");
        DBG_SECTION_END(rwg, "Building call edges done");
    }

    BBlockT& buildBBlock(const llvm::BasicBlock& B, SubgraphT& subg) {
        DBG_SECTION_BEGIN(rwg, "Building basic block");
        auto& bblock = createBBlock(&B, subg);

        for (auto& I : B) {
            assert(_nodes.find(&I) == _nodes.end()
                    && "Building a node that we already have");

            llvm::errs() << "Building " << I << "\n";
            const auto& nds = createNode(&I);
            for (auto *node : nds) {
                bblock.append(node);
            }

            _nodes[&I] = nds.getRepresentant();
        }

        DBG_SECTION_END(rwg, "Building basic block done");
        return bblock;
    }

    void buildSubgraph(const llvm::Function& F) {
        using namespace llvm;

        DBG_SECTION_BEGIN(rwg, "Building the subgraph for " << F.getName().str());
        assert(_subgraphs.find(&F) == _subgraphs.end()
                && "Already have that subgraph");
        auto& subg = createSubgraph(&F);
        _subgraphs[&F] = &subg;

        DBG(rwg, "Building basic blocks of " << F.getName().str());
        // do a walk through basic blocks such that all predecessors of
        // a block are searched before the block itself
        // (operands must be created before their use)
        std::unordered_map<const BasicBlock *, unsigned> visited;
        visited.reserve(F.size());
        auto &entry = F.getEntryBlock();

        // XXX: we could optimize this (vector set?)
        std::set<const BasicBlock *> queue;
        assert(pred_size(&entry) == 0);
        visited[&entry] = 0;

        auto get_ready_block = [&]() -> const BasicBlock * {
            for (auto *b : queue) {
                if (visited[b] == 0) {
                    queue.erase(b);
                    return b;
                }
            }
            return nullptr;
        };

        queue.insert(&entry);
        while (true) {
            auto *cur = get_ready_block();
            if (!cur)
                break;

            auto& bblock = buildBBlock(*cur, subg);
            // FIXME: do something with bblock...

            for (auto *succ : successors(cur)) {
                auto it = visited.find(succ);
                if (it == visited.end()) {
                    visited.emplace_hint(it, succ, pred_size(succ) - 1);
                    queue.insert(succ);
                } else {
                    --it->second;
                }
            }
        }

        DBG(rwg, "Building CFG");
        buildCFG(F, subg);

        DBG_SECTION_END(rwg, "Building the subgraph done");
    }

    void buildGlobals() {
        DBG_SECTION_BEGIN(rwg, "Building globals");
        DBG_SECTION_END(rwg, "Building globals done");
    }

public:
    GraphBuilder(const llvm::Module *m) : _module(m) {}
    virtual ~GraphBuilder() = default;

    const llvm::Module *getModule() const { return _module; }
    const llvm::DataLayout *getDataLayout() const { return &_module->getDataLayout(); }

    const NodesMappingT& getNodesMapping() const {
        return _nodes;
    }

    NodeT *getNode(const llvm::Value *v) {
        auto it = _nodes.find(v);
        return it == _nodes.end() ? nullptr : it->second;
    }

    const NodeT *getNode(const llvm::Value *v) const {
        auto it = _nodes.find(v);
        return it == _nodes.end() ? nullptr : it->second;
    }

    SubgraphT *getSubgraph(const llvm::Function *f) {
        auto it = _subgraphs.find(f);
        return it == _subgraphs.end() ? nullptr : it->second;
    }

    const SubgraphT *getSubgraph(const llvm::Function *f) const {
        auto it = _subgraphs.find(f);
        return it == _subgraphs.end() ? nullptr : it->second;
    }

    virtual NodesSeq<NodeT> createNode(const llvm::Value *) = 0;
    virtual BBlockT& createBBlock(const llvm::BasicBlock *, SubgraphT&) = 0;
    virtual SubgraphT& createSubgraph(const llvm::Function *) = 0;

    void buildFromLLVM() {
        assert(_module && "Do not have the LLVM module");

        buildGlobals();

        // build only reachable calls from CallGraph
        // (if given as an argument)
        // FIXME: do a walk on reachable blocks so that
        // we respect the domination properties of instructions
        for (auto& F : *_module) {
            if (!F.isDeclaration()) {
                buildSubgraph(F);
            }
        }

        // add call-edges
        buildICFG();
    }
};


class LLVMReadWriteGraphBuilder : public GraphBuilder<RWNode, RWBBlock, RWSubgraph> {
    const LLVMDataDependenceAnalysisOptions& _options;
    // points-to information
    dg::LLVMPointerAnalysis *PTA;
    // even the data-flow analysis needs uses to have the mapping of llvm values
    bool buildUses{true};
    // optimization for reaching-definitions analysis
    // TODO: do not do this while building graph, but let the analysis
    // modify the graph itself (or forget it some other way as we'll
    // have the interprocedural graph)
    // bool forgetLocalsAtReturn{false};

    ReadWriteGraph graph;

    //RWNode& getOperand(const llvm::Value *) override;
    NodesSeq<RWNode> createNode(const llvm::Value *) override;
    RWBBlock& createBBlock(const llvm::BasicBlock *, RWSubgraph& subg) override {
        return subg.createBBlock();
    }

    RWSubgraph& createSubgraph(const llvm::Function *) override {
        return graph.createSubgraph();
    }

    /*
    std::map<const llvm::CallInst *, RWNode *> threadCreateCalls;
    std::map<const llvm::CallInst *, RWNode *> threadJoinCalls;

    // mapping of call nodes to called subgraphs
    std::map<std::pair<RWNode *, RWNode *>, std::set<Subgraph *>> calls;
    */

    RWNode& create(RWNodeType t) { return graph.create(t); }

public:
    LLVMReadWriteGraphBuilder(const llvm::Module *m,
                              dg::LLVMPointerAnalysis *p,
                              const LLVMDataDependenceAnalysisOptions& opts)
        : GraphBuilder(m), _options(opts), PTA(p) {}

    ReadWriteGraph&& build() {
        buildFromLLVM();
        
        auto *entry = getModule()->getFunction(_options.entryFunction);
        assert(entry && "Did not find the entry function");
        graph.setEntry(getSubgraph(entry));

        return std::move(graph);
    }

    RWNode *getOperand(const llvm::Value *val);

    std::vector<DefSite> mapPointers(const llvm::Value *where,
                                     const llvm::Value *val,
                                     Offset size);

    RWNode *createStore(const llvm::Instruction *Inst);
    RWNode *createLoad(const llvm::Instruction *Inst);
    RWNode *createAlloc(const llvm::Instruction *Inst);
    RWNode *createDynAlloc(const llvm::Instruction *Inst, AllocationFunction type);
    RWNode *createRealloc(const llvm::Instruction *Inst);
    RWNode *createReturn(const llvm::Instruction *Inst);

/*
    RWNode *funcFromModel(const FunctionModel *model, const llvm::CallInst *);

    void addNode(const llvm::Value *val, RWNode *node)
    {
        auto it = nodes_map.find(val);
        assert(it == nodes_map.end() && "Adding a node that we already have");

        nodes_map.emplace_hint(it, val, node);
    }

    Block& buildBlock(Subgraph& subg, const llvm::BasicBlock& block);
    Block& buildBlockNodes(Subgraph& subg, const llvm::BasicBlock& block);
    Subgraph& buildFunction(const llvm::Function& F);
    Subgraph *getOrCreateSubgraph(const llvm::Function *F);


    std::pair<RWNode *, RWNode *>
    createCallToFunction(const llvm::Function *F, const llvm::CallInst *CInst);

    std::pair<RWNode *, RWNode *>
    createCall(const llvm::Instruction *Inst);

    RWNode * createCallToZeroSizeFunction(const llvm::Function *function,
                                         const llvm::CallInst *CInst);

    std::pair<RWNode *, RWNode *>
    createCallToFunctions(const std::vector<const llvm::Function *> &functions,
                          const llvm::CallInst *CInst);

    RWNode * createPthreadCreateCalls(const llvm::CallInst *CInst);
    RWNode * createPthreadJoinCall(const llvm::CallInst *CInst);
    RWNode * createPthreadExitCall(const llvm::CallInst *CInst);

    RWNode *createIntrinsicCall(const llvm::CallInst *CInst);
    RWNode *createUndefinedCall(const llvm::CallInst *CInst);

    bool isInlineAsm(const llvm::Instruction *instruction);

    void matchForksAndJoins();
    */
};

} // namespace dda
} // namespace dg

#endif
