#ifndef _LLVM_DG_POINTER_SUBGRAPH_H_
#define _LLVM_DG_POINTER_SUBGRAPH_H_

#include <unordered_map>

#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Constants.h>

#include "llvm/MemAllocationFuncs.h"
#include "analysis/PointsTo/PointerSubgraph.h"
#include "analysis/PointsTo/PointsToMapping.h"
#include "analysis/PointsTo/Pointer.h"

namespace dg {
namespace analysis {
namespace pta {

using PSNodesSeq = std::pair<PSNode *, PSNode *>;

class LLVMPointerSubgraphBuilder
{
    PointerSubgraph PS;
    // mapping from llvm values to PSNodes that contain
    // the points-to information
    PointsToMapping<const llvm::Value *> mapping;

    const llvm::Module *M;
    const llvm::DataLayout *DL;
    Offset::type field_sensitivity;
    // flag that says whether we are building normally,
    // or the analysis is already running and we are building
    // some new parts of already built graph.
    // This is important with function pointer calls
    bool ad_hoc_building = false;
    // flag that determines whether invalidate nodes
    // should be created
    bool invalidate_nodes = false;

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

    void buildPointerSubgraphBlock(const llvm::BasicBlock& block);

    PSNodesSeq buildArguments(const llvm::Function& F);
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

    // here we'll keep first and last nodes of every built block and
    // connected together according to successors
    std::map<const llvm::BasicBlock *, PSNodesSeq> built_blocks;

public:
    const PointerSubgraph *getPS() const { return &PS; }

    // \param field_sensitivity -- how much should be the PS field sensitive:
    //        Offset::UNKNOWN means full field sensitivity, 0 means field insensivity
    //        (every pointer with offset greater than 0 will have Offset::UNKNOWN)
    LLVMPointerSubgraphBuilder(const llvm::Module *m,
                               Offset::type field_sensitivity = Offset::UNKNOWN)
        : PS(), M(m), DL(new llvm::DataLayout(m)), field_sensitivity(field_sensitivity)
        {}

    ~LLVMPointerSubgraphBuilder();

    PointerSubgraph *buildLLVMPointerSubgraph();

    PSNodesSeq
    createFuncptrCall(const llvm::CallInst *CInst,
                      const llvm::Function *F);


    // let the user get the nodes map, so that we can
    // map the points-to informatio back to LLVM nodes
    const std::unordered_map<const llvm::Value *, PSNodesSeq>&
                                getNodesMap() const { return nodes_map; }

    // this is the same as the getNode, but it
    // creates ConstantExpr
    PSNode *getPointsTo(const llvm::Value *val)
    {
        PSNode *n = getMapping(val);
        if (!n)
            n = getConstant(val);

        return n;
    }

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
    PSNode *createDynamicAlloc(const llvm::CallInst *CInst, MemAllocationFuncs type);
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
    void addArgumentsOperands(const llvm::Function *F,
                              const llvm::CallInst *CI = nullptr);
    void addVariadicArgumentOperands(const llvm::Function *F, PSNode *arg);
    void addVariadicArgumentOperands(const llvm::Function *F,
                                     const llvm::CallInst *CI,
                                     PSNode *arg);

    void addReturnNodeOperands(const llvm::Function *F,
                               PSNode *ret,
                               const llvm::CallInst *CI = nullptr);

    void addReturnNodeOperand(const llvm::CallInst *CI, PSNode *op);
    void addReturnNodeOperand(const llvm::Function *F, PSNode *op);
    void addInterproceduralOperands(const llvm::Function *F,
                                    Subgraph& subg,
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
    PSNodesSeq createDynamicMemAlloc(const llvm::CallInst *CInst, MemAllocationFuncs type);
    PSNodesSeq createRealloc(const llvm::CallInst *CInst);
    PSNodesSeq createUnknownCall(const llvm::CallInst *CInst);
    PSNodesSeq createIntrinsic(const llvm::Instruction *Inst);
    PSNodesSeq createVarArg(const llvm::IntrinsicInst *Inst);
};

/// --------------------------------------------------------
// Helper functions
/// --------------------------------------------------------
inline unsigned getPointerBitwidth(const llvm::DataLayout *DL,
                                          const llvm::Value *ptr)

{
    const llvm::Type *Ty = ptr->getType();
    return DL->getPointerSizeInBits(Ty->getPointerAddressSpace());
}

inline uint64_t getConstantValue(const llvm::Value *op)
{
    using namespace llvm;

    //XXX: we should get rid of this dependency
    static_assert(sizeof(Offset::type) == sizeof(uint64_t),
                  "The code relies on Offset::type having 8 bytes");

    uint64_t size = Offset::UNKNOWN;
    if (const ConstantInt *C = dyn_cast<ConstantInt>(op)) {
        size = C->getLimitedValue();
    }

    // size is ~((uint64_t)0) if it is unknown
    return size;
}

// get size of memory allocation argument
inline uint64_t getConstantSizeValue(const llvm::Value *op) {
    auto sz = getConstantValue(op);
    // if the size is unknown, make it 0, so that pointer
    // analysis correctly computes offets into this memory
    // (which is always UNKNOWN)
    if (sz == ~static_cast<uint64_t>(0))
        return 0;
    return sz;
}

inline uint64_t getAllocatedSize(const llvm::AllocaInst *AI,
                                 const llvm::DataLayout *DL)
{
    llvm::Type *Ty = AI->getAllocatedType();
    if (!Ty->isSized())
            return 0;

    if (AI->isArrayAllocation()) {
        return getConstantSizeValue(AI->getArraySize()) * DL->getTypeAllocSize(Ty);
    } else
        return DL->getTypeAllocSize(Ty);
}

inline bool isConstantZero(const llvm::Value *val)
{
    using namespace llvm;

    if (const ConstantInt *C = dyn_cast<ConstantInt>(val))
        return C->isZero();

    return false;
}

inline bool isRelevantIntrinsic(const llvm::Function *func)
{
    using namespace llvm;

    switch (func->getIntrinsicID()) {
        case Intrinsic::memmove:
        case Intrinsic::memcpy:
        case Intrinsic::vastart:
        case Intrinsic::stacksave:
        case Intrinsic::stackrestore:
            return true;
        // case Intrinsic::memset:
        default:
            return false;
    }
}

inline bool isInvalid(const llvm::Value *val)
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
            if (F && F->isIntrinsic() && !isRelevantIntrinsic(F))
                return true;
        }
    }

    return false;
}

inline bool memsetIsZeroInitialization(const llvm::IntrinsicInst *I)
{
    return isConstantZero(I->getOperand(1));
}

// recursively find out if type contains a pointer type as a subtype
// (or if it is a pointer type itself)
inline bool tyContainsPointer(const llvm::Type *Ty)
{
    if (Ty->isAggregateType()) {
        for (auto I = Ty->subtype_begin(), E = Ty->subtype_end();
             I != E; ++I) {
            if (tyContainsPointer(*I))
                return true;
        }
    } else
        return Ty->isPointerTy();

    return false;
}

inline bool typeCanBePointer(const llvm::DataLayout *DL, llvm::Type *Ty)
{
    if (Ty->isPointerTy())
        return true;

    if (Ty->isIntegerTy() && Ty->isSized())
        return DL->getTypeSizeInBits(Ty)
                >= DL->getPointerSizeInBits(/*Ty->getPointerAddressSpace()*/);

    return false;
}


} // namespace pta
} // namespace dg
} // namespace analysis

#endif
