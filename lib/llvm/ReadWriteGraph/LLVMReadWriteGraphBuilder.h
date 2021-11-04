#ifndef LLVM_DG_RWG_BUILDER_H
#define LLVM_DG_RWG_BUILDER_H

#include <memory>
#include <set>
#include <unordered_map>

#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_os_ostream.h>

#include "dg/ReadWriteGraph/ReadWriteGraph.h"
#include "dg/llvm/CallGraph/CallGraph.h"
#include "dg/llvm/DataDependence/LLVMDataDependenceAnalysisOptions.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "llvm/GraphBuilder.h"

#ifndef NDEBUG
#include "dg/util/debug.h"
#endif // NDEBUG

namespace dg {
namespace dda {

class LLVMReadWriteGraphBuilder
        : public GraphBuilder<RWNode, RWBBlock, RWSubgraph> {
    const LLVMDataDependenceAnalysisOptions &_options;
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

    // RWNode& getOperand(const llvm::Value *) override;
    NodesSeq<RWNode> createNode(const llvm::Value * /*unused*/) override;
    RWBBlock &createBBlock(const llvm::BasicBlock * /*unused*/,
                           RWSubgraph &subg) override {
        return subg.createBBlock();
    }

    RWSubgraph &createSubgraph(const llvm::Function *F) override {
        auto &subg = graph.createSubgraph();
        subg.setName(F->getName().str());
        return subg;
    }

    std::map<const llvm::CallInst *, RWNode *> threadCreateCalls;
    std::map<const llvm::CallInst *, RWNode *> threadJoinCalls;

    /*
    // mapping of call nodes to called subgraphs
    std::map<std::pair<RWNode *, RWNode *>, std::set<Subgraph *>> calls;
    */

    RWNode &create(RWNodeType t) { return graph.create(t); }

  public:
    LLVMReadWriteGraphBuilder(const llvm::Module *m, dg::LLVMPointerAnalysis *p,
                              const LLVMDataDependenceAnalysisOptions &opts)
            : GraphBuilder(m), _options(opts), PTA(p) {}

    ReadWriteGraph &&build() {
        // FIXME: this is a bit of a hack
        if (!PTA->getOptions().isSVF()) {
            auto *dgpta = static_cast<DGLLVMPointerAnalysis *>(PTA);
            llvmdg::CallGraph CG(dgpta->getPTA()->getPG()->getCallGraph());
            buildFromLLVM(&CG);
        } else {
            buildFromLLVM();
        }

        auto *entry = getModule()->getFunction(_options.entryFunction);
        assert(entry && "Did not find the entry function");
        graph.setEntry(getSubgraph(entry));

        return std::move(graph);
    }

    RWNode *getOperand(const llvm::Value *val);

    std::vector<DefSite> mapPointers(const llvm::Value *where,
                                     const llvm::Value *val, Offset size);

    RWNode *createStore(const llvm::Instruction *Inst);
    RWNode *createLoad(const llvm::Instruction *Inst);
    RWNode *createAtomicRMW(const llvm::Instruction *Inst);
    RWNode *createAlloc(const llvm::Instruction *Inst);
    RWNode *createDynAlloc(const llvm::Instruction *Inst,
                           AllocationFunction type);
    RWNode *createRealloc(const llvm::Instruction *Inst);
    RWNode *createReturn(const llvm::Instruction *Inst);

    void addReallocUses(const llvm::Instruction *Inst, RWNode &node,
                        uint64_t size);

    RWNode *funcFromModel(const FunctionModel *model,
                          const llvm::CallInst * /*CInst*/);

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

    // void matchForksAndJoins();
};

struct ValInfo {
    const llvm::Value *v;
    ValInfo(const llvm::Value *val) : v(val) {}
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const ValInfo &vi);

} // namespace dda
} // namespace dg

#endif
