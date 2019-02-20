#ifndef _LLVM_DG_POINTER_SUBGRAPH_H_
#define _LLVM_DG_POINTER_SUBGRAPH_H_

#include <unordered_map>

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
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Constants.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h"

#include "dg/analysis/PointsTo/PointerSubgraph.h"
#include "dg/analysis/PointsTo/PointsToMapping.h"
#include "dg/analysis/PointsTo/Pointer.h"


namespace dg {

namespace analysis {
namespace pta {

using PSNodesSeq = std::pair<PSNode *, PSNode *>;

class LLVMPointerSubgraphBuilder
{
    PointerSubgraph PS{};
    // mapping from llvm values to PSNodes that contain
    // the points-to information
    PointsToMapping<const llvm::Value *> mapping;

    const llvm::Module *M;
    const llvm::DataLayout *DL;
    LLVMPointerAnalysisOptions _options;

    // flag that says whether we are building normally,
    // or the analysis is already running and we are building
    // some new parts of already built graph.
    // This is important with function pointer calls
    bool ad_hoc_building = false;
    // flag that determines whether invalidate nodes
    // should be created
    bool invalidate_nodes = false;

    bool threads_ = false;

    struct Subgraph {
        Subgraph(PSNode *r1, PSNode *r2, PSNode *va = nullptr)
            : root(r1), ret(r2), vararg(va) {}
        /*
        Subgraph(PSNode *r1, PSNode *r2, PSNode *va,
                 std::vector<const llvm::BasicBlock *>&& blcks)
            : root(r1), ret(r2), vararg(va), llvmBlocks(blcks) {}
            */
        Subgraph() = default;
        Subgraph(Subgraph&&) = default;
        Subgraph(const Subgraph&) = delete;

        // first and last nodes of the subgraph
        PSNode *root{nullptr};
        PSNode *ret{nullptr};

        std::set<PSNode *> returnNodes;
        // this is the node where we gather the variadic-length arguments
        PSNode *vararg{nullptr};

        // reachable LLVM block (those block for which we built the instructions)
        std::vector<const llvm::BasicBlock *> llvmBlocks;
        bool has_structure = false;
    };

    // build pointer state subgraph for given graph
    // \return   root node of the graph
    Subgraph& buildFunction(const llvm::Function& F);
    PSNodesSeq buildInstruction(const llvm::Instruction&);

    PSNodesSeq buildPointerSubgraphBlock(const llvm::BasicBlock& block,
                                         PSNode *parent);

    void buildArguments(const llvm::Function& F,
                        PSNode *parent);
    PSNodesSeq buildArgumentsStructure(const llvm::Function& F);
    PSNodesSeq buildGlobals();

    // add edges that are derived from CFG to the subgraph
    void addProgramStructure();
    void addProgramStructure(const llvm::Function *F, Subgraph& subg);
    PSNodesSeq buildBlockStructure(const llvm::BasicBlock& block);
    void blockAddCalls(const llvm::BasicBlock& block);

    // map of all nodes we created - use to look up operands
    std::unordered_map<const llvm::Value *, PSNodesSeq > nodes_map;
    // map of all built subgraphs - the value type is a pair (root, return)
    std::unordered_map<const llvm::Function *, Subgraph> subgraphs_map;

    std::map<PSNode *, PSNodeFork *> threadCreateCalls;
    std::map<PSNode *, PSNodeJoin *> threadJoinCalls;

    // here we'll keep first and last nodes of every built block and
    // connected together according to successors
    std::map<const llvm::BasicBlock *, PSNodesSeq> built_blocks;

public:
    const PointerSubgraph *getPS() const { return &PS; }

    inline bool threads() { return threads_; }

    LLVMPointerSubgraphBuilder(const llvm::Module *m, const LLVMPointerAnalysisOptions& opts)
        : M(m), DL(new llvm::DataLayout(m)), _options(opts), threads_(opts.threads) {}

    ~LLVMPointerSubgraphBuilder();

    PointerSubgraph *buildLLVMPointerSubgraph();

    bool validateSubgraph(bool no_connectivity = false) const;

    void setAdHocBuilding(bool adHoc) { ad_hoc_building = adHoc; }

    PSNodesSeq
    createFuncptrCall(const llvm::CallInst *CInst,
                      const llvm::Function *F);

    static bool callIsCompatible(PSNode *call, PSNode *func);

    // Insert a call of a function into an already existing graph.
    // The call will be inserted betwee the callsite and
    // the return from the call nodes.
    void insertFunctionCall(PSNode *callsite, PSNode *called);
    void insertPthreadCreateByPtrCall(PSNode *callsite);
    void insertPthreadJoinByPtrCall(PSNode *callsite);

    PSNodesSeq createFork(const llvm::CallInst *CInst);
    PSNodesSeq createJoin(const llvm::CallInst *CInst);
    PSNodesSeq createPthreadExit(const llvm::CallInst *CInst);

    bool addFunctionToFork(PSNode * function,
                           PSNodeFork *forkNode);
    bool addFunctionToJoin(PSNode *function,
                           PSNodeJoin * joinNode);

    bool matchJoinToRightCreate(PSNode *pthreadJoinCall);
    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, PSNodesSeq>&
                                getNodesMap() const { return nodes_map; }

    std::vector<PSNode *> getFunctionNodes(const llvm::Function *F) const;

    // this is the same as the getNode, but it
    // creates ConstantExpr
    PSNode *getPointsTo(const llvm::Value *val)
    {
        PSNode *n = getMapping(val);
        if (!n)
            n = getConstant(val);

        return n;
    }

    std::vector<PSNode *>
    getPointsToFunctions(const llvm::Value *calledValue);

    std::map<PSNode *, PSNodeJoin *>
    getJoins() const;

    std::map<PSNode *, PSNodeFork *>
    getForks() const;
    void setInvalidateNodesFlag(bool value) 
    {
        assert(PS.getRoot() == nullptr &&
                "This function must be called before building PS");
        this->invalidate_nodes = value;
    }

    void composeMapping(PointsToMapping<PSNode *>&& rhs) {
        mapping.compose(std::move(rhs));
    }

private:

    // create subgraph of function @F (the nodes)
    // and call+return nodes to/from it. This function
    // won't add the CFG edges if not 'ad_hoc_building'
    // is set to true
    PSNodesSeq
    createCallToFunction(const llvm::CallInst *, const llvm::Function *);

    PSNode *getMapping(const llvm::Value *val) {
        return mapping.get(val);
    }

    PSNode *getNode(const llvm::Value *val)
    {
        auto it = nodes_map.find(val);
        if (it == nodes_map.end())
            return nullptr;

        // the node corresponding to the real llvm value
        // is always the last
        //
        // XXX: this holds everywhere except for va_start
        // and realloc sequences. Maybe we should use a new class
        // instead of std::pair to represent the sequence
        return it->second.second;
    }

    void setMapping(const llvm::Value *val, PSNode *node) {
        // if this is a call that returns a pointer,
        // then the points-to is in CALL_RETURN node
        if (node->getType() == PSNodeType::CALL
            || node->getType() == PSNodeType::CALL_FUNCPTR)
            node = node->getPairedNode();

        mapping.add(val, node);
    }

    void addNode(const llvm::Value *val, PSNode *node)
    {
        nodes_map.emplace(val, std::make_pair(node, node));
        node->setUserData(const_cast<llvm::Value *>(val));

        setMapping(val, node);
    }

    void addNode(const llvm::Value *val, PSNodesSeq seq)
    {
        nodes_map.emplace(val, seq);
        seq.second->setUserData(const_cast<llvm::Value *>(val));

        setMapping(val, seq.second);
    }

    bool isRelevantInstruction(const llvm::Instruction& Inst);

    PSNode *createAlloc(const llvm::Instruction *Inst);
    PSNode *createDynamicAlloc(const llvm::CallInst *CInst,
                               AllocationFunction type);
    PSNode *createStore(const llvm::Instruction *Inst);
    PSNode *createLoad(const llvm::Instruction *Inst);
    PSNode *createGEP(const llvm::Instruction *Inst);
    PSNode *createSelect(const llvm::Instruction *Inst);
    PSNode *createPHI(const llvm::Instruction *Inst);
    PSNode *createCast(const llvm::Instruction *Inst);
    PSNode *createReturn(const llvm::Instruction *Inst);
    PSNode *createPtrToInt(const llvm::Instruction *Inst);
    PSNode *createIntToPtr(const llvm::Instruction *Inst);
    PSNode *createAsm(const llvm::Instruction *Inst);

    PSNode *createIrrelevantInst(const llvm::Value *,
                                  bool build_uses = false);
    PSNode *createArgument(const llvm::Argument *);
    void createIrrelevantUses(const llvm::Value *val);

    PSNode *createAdd(const llvm::Instruction *Inst);
    PSNode *createArithmetic(const llvm::Instruction *Inst);
    PSNode *createUnknown(const llvm::Value *val);
    PSNode *createLifetimeEnd(const llvm::Instruction *Inst);
    PSNode *createFree(const llvm::Instruction *Inst);

    PSNode *getOperand(const llvm::Value *val);
    PSNode *tryGetOperand(const llvm::Value *val);
    PSNode *getConstant(const llvm::Value *val);
    PSNode *createConstantExpr(const llvm::ConstantExpr *CE);
    Pointer handleConstantGep(const llvm::GetElementPtrInst *GEP);
    Pointer handleConstantBitCast(const llvm::BitCastInst *BC);
    Pointer handleConstantPtrToInt(const llvm::PtrToIntInst *P2I);
    Pointer handleConstantIntToPtr(const llvm::IntToPtrInst *I2P);
    Pointer handleConstantAdd(const llvm::Instruction *Inst);
    Pointer handleConstantArithmetic(const llvm::Instruction *Inst);
    Pointer getConstantExprPointer(const llvm::ConstantExpr *CE);

    void checkMemSet(const llvm::Instruction *Inst);
    void addPHIOperands(PSNode *node, const llvm::PHINode *PHI);
    void addPHIOperands(const llvm::Function& F);
    void addArgumentOperands(const llvm::Function *F, PSNode *arg, int idx);
    void addArgumentOperands(const llvm::CallInst *CI, PSNode *arg, int idx);
    void addArgumentOperands(const llvm::CallInst &CI, PSNode &node);
    void addArgumentsOperands(const llvm::Function *F,
                              const llvm::CallInst *CI = nullptr,
                              int index = 0);
    void addVariadicArgumentOperands(const llvm::Function *F, PSNode *arg);
    void addVariadicArgumentOperands(const llvm::Function *F,
                                     const llvm::CallInst *CI,
                                     PSNode *arg);

    void addReturnNodeOperands(const llvm::Function *F,
                               PSNode *ret,
                               PSNode *callNode = nullptr);

    void addReturnNodeOperand(PSNode *callNode, PSNode *op);
    void addReturnNodeOperand(const llvm::Function *F, PSNode *op);
    void addInterproceduralOperands(const llvm::Function *F,
                                    Subgraph& subg,
                                    const llvm::CallInst *CI = nullptr,
                                    PSNode *callNode = nullptr);
    void addInterproceduralPthreadOperands(const llvm::Function *F,
                                           const llvm::CallInst *CI = nullptr);

    PSNodesSeq createExtract(const llvm::Instruction *Inst);
    PSNodesSeq createCall(const llvm::Instruction *Inst);
    PSNodesSeq createFunctionCall(const llvm::CallInst *, const llvm::Function *);
    PSNodesSeq createFuncptrCall(const llvm::CallInst *, const llvm::Value *);

    Subgraph& createOrGetSubgraph(const llvm::Function *);


    PSNode *handleGlobalVariableInitializer(const llvm::Constant *C,
                                            PSNodeAlloc *node,
                                            PSNode *last = nullptr,
                                            uint64_t offset = 0);

    PSNode *createMemTransfer(const llvm::IntrinsicInst *Inst);

    PSNodesSeq createMemSet(const llvm::Instruction *);
    PSNodesSeq createDynamicMemAlloc(const llvm::CallInst *CInst,
                                     AllocationFunction type);
    PSNodesSeq createRealloc(const llvm::CallInst *CInst);
    PSNodesSeq createUnknownCall(const llvm::CallInst *CInst);
    PSNodesSeq createIntrinsic(const llvm::Instruction *Inst);
    PSNodesSeq createVarArg(const llvm::IntrinsicInst *Inst);
};

/// --------------------------------------------------------
// Helper functions
/// --------------------------------------------------------
inline bool isRelevantIntrinsic(const llvm::Function *func, bool invalidate_nodes)
{
    using namespace llvm;

    switch (func->getIntrinsicID()) {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
        case Intrinsic::vastart:
        case Intrinsic::stacksave:
        case Intrinsic::stackrestore:
            return true;
        case Intrinsic::lifetime_end:
            return invalidate_nodes;
        // case Intrinsic::memset:
        default:
            return false;
    }
}

inline bool isInvalid(const llvm::Value *val, bool invalidate_nodes)
{
    using namespace llvm;

    if (!isa<Instruction>(val)) {
        if (!isa<Argument>(val) && !isa<GlobalValue>(val))
            return true;
    } else {
        if (isa<ICmpInst>(val) || isa<FCmpInst>(val)
            || isa<DbgValueInst>(val) || isa<BranchInst>(val)
            || isa<SwitchInst>(val))
            return true;

        const CallInst *CI = dyn_cast<CallInst>(val);
        if (CI) {
            const Function *F = CI->getCalledFunction();
            if (F && F->isIntrinsic() && !isRelevantIntrinsic(F, invalidate_nodes))
                return true;
        }
    }

    return false;
}

} // namespace pta
} // namespace dg
} // namespace analysis

#endif
