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
#include "dg/ADT/SetQueue.h"

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

    NodesSeq(NodesSeq&&) = default;
    NodesSeq(const NodesSeq&) = default;

    NodeT *setRepresentant(NodeT *r) {
        representant = r;
    }

    NodeT *getRepresentant() const {
        return representant;
    }

    bool empty() const { return nodes.empty(); }

    auto begin() -> decltype(nodes.begin()) { return nodes.begin(); }
    auto end() -> decltype(nodes.end()) { return nodes.end(); }
    auto begin() const -> decltype(nodes.begin()) { return nodes.begin(); }
    auto end() const -> decltype(nodes.end()) { return nodes.end(); }
};
 

template <typename NodeT, typename BBlockT, typename SubgraphT>
class GraphBuilder {
    struct SubgraphInfo {
        using BlocksMappingT
            = std::unordered_map<const llvm::BasicBlock *, BBlockT *>;

        SubgraphT& subgraph;
        BlocksMappingT blocks{};

        SubgraphInfo(SubgraphT& s) : subgraph(s) {}
        SubgraphInfo(SubgraphInfo&&) = default;
        SubgraphInfo(const SubgraphInfo&) = delete;
    };

    using GlobalsT = std::vector<NodeT *>;
    using NodesMappingT = std::unordered_map<const llvm::Value *, NodesSeq<NodeT>>;
    using ValuesMappingT = std::unordered_map<const NodeT *, const llvm::Value *>;
    using SubgraphsMappingT = std::unordered_map<const llvm::Value *, SubgraphInfo>;

    const llvm::Module *_module;

    SubgraphsMappingT _subgraphs;
    NodesMappingT _nodes;
    ValuesMappingT _nodeToValue;
    GlobalsT _globals;

    void buildCFG(SubgraphInfo& subginfo) {
        for (auto& it : subginfo.blocks) {
            auto llvmblk = it.first;
            auto bblock = it.second;

            for (auto *succ : successors(llvmblk)) {
                auto succit = subginfo.blocks.find(succ);
                assert((succit != subginfo.blocks.end())
                       && "Do not have the block built");

                bblock->addSuccessor(succit->second);
            }
        }
    }

    void buildGlobals() {
        DBG_SECTION_BEGIN(rwg, "Building globals");

        for (auto& G : _module->globals()) {
            // every global node is like memory allocation
            auto cur = buildNode(&G);
            _globals.insert(_globals.end(), cur.begin(), cur.end());

            // add the initial global definitions
            //if (auto GV = llvm::dyn_cast<llvm::GlobalVariable>(&G)) {
            //    auto size = llvmutils::getAllocatedSize(GV->getType()->getContainedType(0),
             //                                           &_module->getDataLayout());
//                if (size == 0)
//                    size = Offset::UNKNOWN;

//                cur->addDef(cur, 0, size, true /* strong update */);
        }

        DBG_SECTION_END(rwg, "Building globals done");
    }


protected:

    NodesSeq<NodeT> buildNode(const llvm::Value *val) {
        auto it = _nodes.find(val);
        if (it != _nodes.end()) {
            return it->second;
        }

        const auto& nds = createNode(val);
        assert((nds.getRepresentant() || nds.empty())
                && "Built node sequence has no representant");

        if (auto *repr = nds.getRepresentant()) {
            _nodes.emplace(val, std::move(nds));

            assert((_nodeToValue.find(repr) == _nodeToValue.end())
                    && "Mapping a node that we already have");
            _nodeToValue[repr] = val;
        }

        return nds;
    }

    BBlockT& buildBBlock(const llvm::BasicBlock& B, SubgraphInfo& subginfo) {
        DBG_SECTION_BEGIN(rwg, "Building basic block");
        auto& bblock = createBBlock(&B, subginfo.subgraph);
        assert(subginfo.blocks.find(&B) == subginfo.blocks.end()
                && "Already have this basic block");
        subginfo.blocks[&B] = &bblock;

        for (auto& I : B) {
            for (auto *node : buildNode(&I)) {
                bblock.append(node);
            }
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
        auto& subginfo = _subgraphs.emplace(&F, subg).first->second;

        DBG(rwg, "Building basic blocks of " << F.getName().str());
        // do a walk through basic blocks such that all predecessors of
        // a block are searched before the block itself
        // (operands must be created before their use)
        auto &entry = F.getEntryBlock();

        // process the block in BFS order to make sure
        // the operands are created before their use
        ADT::SetQueue<ADT::QueueFIFO<const llvm::BasicBlock *>> queue;
        queue.push(&entry);

        while (!queue.empty()) {
            auto *cur = queue.pop();

            buildBBlock(*cur, subginfo);

            for (auto *succ : successors(cur)) {
                queue.push(succ);
            }
        }

        DBG(rwg, "Building CFG");
        buildCFG(subginfo);

        DBG_SECTION_END(rwg, "Building the subgraph done");
    }

public:
    GraphBuilder(const llvm::Module *m) : _module(m) {}
    virtual ~GraphBuilder() = default;

    const llvm::Module *getModule() const { return _module; }
    const llvm::DataLayout *getDataLayout() const { return &_module->getDataLayout(); }

    const GlobalsT& getGlobals() const { return _globals; }

    const NodesMappingT& getNodesMapping() const {
        return _nodes;
    }

    const ValuesMappingT& getValuesMapping() const {
        return _nodeToValue;
    }

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

    RWNode *funcFromModel(const FunctionModel *model, const llvm::CallInst *);

    NodesSeq<RWNode> createCall(const llvm::Instruction *Inst);

    RWNode *createCallToUndefinedFunction(const llvm::Function *function,
                                          const llvm::CallInst *CInst);

    NodesSeq<RWNode>
    createCallToFunctions(const std::vector<const llvm::Function *> &functions,
                          const llvm::CallInst *CInst);

    RWNode *createPthreadCreateCalls(const llvm::CallInst *CInst);
    RWNode *createPthreadJoinCall(const llvm::CallInst *CInst);
    RWNode *createPthreadExitCall(const llvm::CallInst *CInst);

    RWNode *createIntrinsicCall(const llvm::CallInst *CInst);
    RWNode *createUnknownCall(const llvm::CallInst *CInst);

    //void matchForksAndJoins();
};

struct ValInfo {
    const llvm::Value *v;
    ValInfo(const llvm::Value *val) : v(val) {}
};

llvm::raw_ostream& operator<<(llvm::raw_ostream& os, const ValInfo& vi);


} // namespace dda
} // namespace dg

#endif
