#ifndef _LLVM_DG_PSS_H_
#define _LLVM_DG_PSS_H_

#include <unordered_map>
#include <cassert>

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Constant.h>
#include <llvm/Support/raw_os_ostream.h>

#include "analysis/PSS.h"
#include "llvm/PSS.h"

#ifdef DEBUG_ENABLED
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#endif

namespace dg {
namespace analysis {

#ifdef DEBUG_ENABLED
static std::string
getInstName(const llvm::Value *val)
{
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    assert(val);
    ro << *val;
    ro.flush();

    // break the string if it is too long
    return ostr.str();
}
#endif

// map of all nodes we created - use to look up operands
std::unordered_map<const llvm::Value *, PSSNode *> nodes_map;

enum MemAllocationFuncs {
    NONEMEM = 0,
    MALLOC,
    CALLOC,
    ALLOCA,
};

static int getMemAllocationFunc(const llvm::Function *func)
{
    if (!func || !func->hasName())
        return NONEMEM;

    const char *name = func->getName().data();
    if (strcmp(name, "malloc") == 0)
        return MALLOC;
    else if (strcmp(name, "calloc") == 0)
        return CALLOC;
    else if (strcmp(name, "alloca") == 0)
        return ALLOCA;
    else if (strcmp(name, "realloc") == 0)
        // FIXME
        assert(0 && "realloc not implemented yet");

    return NONEMEM;
}

static inline unsigned getPointerBitwidth(const llvm::DataLayout *DL,
                                          const llvm::Value *ptr)

{
    const llvm::Type *Ty = ptr->getType();
    return DL->getPointerSizeInBits(Ty->getPointerAddressSpace());
}

static uint64_t getAllocatedSize(llvm::Type *Ty, const llvm::DataLayout *DL)
{
    // Type can be i8 *null or similar
    if (!Ty->isSized())
            return 0;

    return DL->getTypeAllocSize(Ty);
}

Pointer getConstantExprPointer(const llvm::ConstantExpr *CE,
                               const llvm::DataLayout *DL);

static Pointer handleConstantBitCast(const llvm::BitCastInst *BC,
                                     const llvm::DataLayout *DL)
{
    using namespace llvm;

    if (!BC->isLosslessCast()) {
        errs() << "WARN: Not a loss less cast unhandled ConstExpr"
               << *BC << "\n";
        abort();
        return PointerUnknown;
    }

    const Value *llvmOp = BC->stripPointerCasts();
    PSSNode *op = nodes_map[llvmOp];
    if (!op) {
        // is this recursively created expression? If so, get the pointer for it
        if (isa<ConstantExpr>(llvmOp)) {
            return getConstantExprPointer(cast<ConstantExpr>(llvmOp), DL);
        } else {
            errs() << *llvmOp << "\n";
            errs() << *BC << "\n";
            assert(0 && "Unsupported bitcast");
        }
    } else {
        assert(op->getType() == pss::CONSTANT
               && "Constant Bitcast on non-constant");

        assert(op->pointsTo.size() == 1
               && "Constant BitCast with not only one pointer");

        return *op->pointsTo.begin();
    }
}

static Pointer handleConstantGep(const llvm::GetElementPtrInst *GEP,
                                 const llvm::DataLayout *DL)
{
    using namespace llvm;

    const Value *op = GEP->getPointerOperand();
    Pointer pointer(UNKNOWN_MEMORY, UNKNOWN_OFFSET);

    // get operand PSSNode - if it exists
    PSSNode *opNode = nodes_map[op];

    // we dont have the operand node... is it constant or constant expr?
    if (!opNode) {
        // is this recursively created expression? If so, get the pointer for it
        if (isa<ConstantExpr>(op)) {
            pointer = getConstantExprPointer(cast<ConstantExpr>(op), DL);
        } else {
            errs() << *op << "\n";
            errs() << *GEP << "\n";
            assert(0 && "Unsupported constant GEP");
        }
    } else {
        assert(opNode->getType() == pss::CONSTANT
                && "ConstantExpr GEP on non-constant");
        assert(opNode->pointsTo.size() == 1
                && "Constant node has more that 1 pointer");
        pointer = *(opNode->pointsTo.begin());
    }

    unsigned bitwidth = getPointerBitwidth(DL, op);
    APInt offset(bitwidth, 0);

    // get offset of this GEP
    if (GEP->accumulateConstantOffset(*DL, offset)) {
        if (offset.isIntN(bitwidth) && !pointer.offset.isUnknown())
            pointer.offset = offset.getZExtValue();
        else
            errs() << "WARN: Offset greater than "
                   << bitwidth << "-bit" << *GEP << "\n";
    }

    return pointer;
}

Pointer getConstantExprPointer(const llvm::ConstantExpr *CE,
                               const llvm::DataLayout *DL)
{
    using namespace llvm;

    Pointer pointer(UNKNOWN_MEMORY, UNKNOWN_OFFSET);
    const Instruction *Inst = const_cast<ConstantExpr*>(CE)->getAsInstruction();

    if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Inst)) {
        pointer = handleConstantGep(GEP, DL);
    } else if (const BitCastInst *BC = dyn_cast<BitCastInst>(Inst)) {
        pointer = handleConstantBitCast(BC, DL);
    } else {
            errs() << "ERR: Unsupported ConstantExpr " << *CE << "\n";
            abort();
    }

    delete Inst;
    return pointer;
}

static PSSNode *createConstantExpr(const llvm::ConstantExpr *CE,
                                   const llvm::DataLayout *DL)
{
    Pointer ptr = getConstantExprPointer(CE, DL);
    PSSNode *node = new PSSNode(pss::CONSTANT, ptr);
    nodes_map[CE] = node;

#ifdef DEBUG_ENABLED
    node->setName(getInstName(CE).c_str());
#endif

    assert(node);
    return node;
}

static PSSNode *createDynamicAlloc(const llvm::CallInst *CInst, int type)
{
    using namespace llvm;

    const Value *op;
    uint64_t size = 0, size2 = 0;
    PSSNode *node = new PSSNode(pss::DYN_ALLOC);

    switch (type) {
        case MALLOC:
            node->setIsHeap();
        case ALLOCA:
            op = CInst->getOperand(0);
            break;
        case CALLOC:
            node->setIsHeap();
            node->setZeroInitialized();
            op = CInst->getOperand(1);
            break;
        default:
            errs() << *CInst << "\n";
            assert(0 && "unknown memory allocation type");
    };

    if (const ConstantInt *C = dyn_cast<ConstantInt>(op)) {
        size = C->getLimitedValue();
        // if the size cannot be expressed as an uint64_t,
        // just set it to 0 (that means unknown)
        if (size == ~((uint64_t) 0))
            size = 0;

        // if this is call to calloc, the size is given
        // in the first argument too
        if (type == CALLOC) {
            C = dyn_cast<ConstantInt>(CInst->getOperand(0));
            if (C) {
                size2 = C->getLimitedValue();
                if (size2 == ~((uint64_t) 0))
                    size2 = 0;
                else
                    // OK, if getting the size fails, we end up with
                    // just 1 * size - still better than 0 and UNKNOWN
                    // (it may be cropped later anyway)
                    size *= size2;
            }
        }
    }

    node->setSize(size);
    return node;
}

static PSSNode *createCall(const llvm::Instruction *Inst)
{
    using namespace llvm;
    const CallInst *CInst = cast<CallInst>(Inst);
    PSSNode *node = nullptr;

    // TODO: we can match the patterns and at least
    // get some points-to information from inline asm.
    if (CInst->isInlineAsm())
        assert(0 && "Inline asm unsupported");

    const Function *func
        = dyn_cast<Function>(CInst->getCalledValue()->stripPointerCasts());

    int type;
    if (func && (type = getMemAllocationFunc(func)))
        node = createDynamicAlloc(CInst, type);

    if (func && func->isIntrinsic())
        assert(0 && "Intrinsic function not implemented yet");

    /*
    // add subgraphs dynamically according the points-to information
    LLVMNode *calledFuncNode = getOperand(node, Inst->getCalledValue(), 0);
    if (!func && calledFuncNode)
        changed |= handleFunctionPtrCall(calledFuncNode, node);

    // function is undefined and returns a pointer?
    // In that case create pointer to unknown location
    // and set this node to point to unknown location
    if ((!func || func->size() == 0) && Ty->isPointerTy())
        return handleUndefinedReturnsPointer(Inst, node);
        */

    if (!node) {
        errs() << *CInst << "\n";
        assert(0 && "Interprocedural analysis not implemented yet");
    }

    nodes_map[CInst] = node;

#ifdef DEBUG_ENABLED
    node->setName(getInstName(CInst).c_str());
#endif

    assert(node);
    return node;
}

static PSSNode *createAlloc(const llvm::Instruction *Inst,
                            const llvm::DataLayout *DL)
{
    PSSNode *node = new PSSNode(pss::ALLOC);
    nodes_map[Inst] = node;

#ifdef DEBUG_ENABLED
    node->setName(getInstName(Inst).c_str());
#endif

    const llvm::AllocaInst *AI = llvm::dyn_cast<llvm::AllocaInst>(Inst);
    if (AI) {
        uint64_t size = getAllocatedSize(AI->getAllocatedType(), DL);
        node->setSize(size);
    }

    assert(node);
    return node;
}

static PSSNode *createStore(const llvm::Instruction *Inst,
                            const llvm::DataLayout *DL)
{
    PSSNode *op1 = nodes_map[Inst->getOperand(0)];
    PSSNode *op2 = nodes_map[Inst->getOperand(1)];

    // the value needs to be a pointer
    assert(Inst->getOperand(0)->getType()->isPointerTy());

    if (!op1) {
        if (const llvm::ConstantExpr *CE
                = llvm::dyn_cast<llvm::ConstantExpr>(Inst->getOperand(0))) {
            op1 = createConstantExpr(CE, DL);
        } else {
            llvm::errs() << *Inst->getOperand(0) << "\n";
            llvm::errs() << *Inst << "\n";
            assert(0 && "Instruction unspported");
        }
    }

    assert(op2 && "Store does not have pointer operand");
    PSSNode *node = new PSSNode(pss::STORE, op1, op2);
    nodes_map[Inst] = node;

#ifdef DEBUG_ENABLED
    node->setName(getInstName(Inst).c_str());
#endif

    assert(node);
    return node;
}

static PSSNode *createLoad(const llvm::Instruction *Inst,
                           const llvm::DataLayout *DL)
{
    const llvm::Value *op = Inst->getOperand(0);
    PSSNode *op1 = nodes_map[op];

    if (!op1) {
        if (const llvm::ConstantExpr *CE
                = llvm::dyn_cast<llvm::ConstantExpr>(op)) {
            op1 = createConstantExpr(CE, DL);
        } else {
            llvm::errs() << *op << "\n";
            llvm::errs() << *Inst << "\n";
            assert(0 && "Instruction unspported");
        }
    }

    PSSNode *node = new PSSNode(pss::LOAD, op1);
    nodes_map[Inst] = node;

#ifdef DEBUG_ENABLED
    node->setName(getInstName(Inst).c_str());
#endif

    assert(node);
    return node;
}

static PSSNode *createGEP(const llvm::Instruction *Inst,
                          const llvm::DataLayout *DL)
{
    using namespace llvm;

    const GetElementPtrInst *GEP = cast<GetElementPtrInst>(Inst);
    const Value *ptrOp = GEP->getPointerOperand();
    unsigned bitwidth = getPointerBitwidth(DL, ptrOp);
    APInt offset(bitwidth, 0);
    PSSNode *node = nullptr;

    PSSNode *op = nodes_map[ptrOp];
    if (!op) {
        if (const llvm::ConstantExpr *CE
                = llvm::dyn_cast<llvm::ConstantExpr>(ptrOp)) {
            op = createConstantExpr(CE, DL);
        } else {
            llvm::errs() << *ptrOp << "\n";
            llvm::errs() << *Inst << "\n";
            assert(0 && "Instruction unspported");
        }
    }

    if (GEP->accumulateConstantOffset(*DL, offset)) {
        if (offset.isIntN(bitwidth))
            node = new PSSNode(pss::GEP, op, offset.getZExtValue());
        else
            errs() << "WARN: GEP offset greater than " << bitwidth << "-bit";
            // fall-through to UNKNOWN_OFFSET in this case
    }

    if (!node)
        node = new PSSNode(pss::GEP, op, UNKNOWN_OFFSET);

    nodes_map[Inst] = node;

#ifdef DEBUG_ENABLED
    node->setName(getInstName(Inst).c_str());
#endif

    assert(node);
    return node;
}

static PSSNode *createCast(const llvm::Instruction *Inst,
                           const llvm::DataLayout *DL)
{
    const llvm::Value *op = Inst->getOperand(0);
    PSSNode *op1 = nodes_map[op];

    if (!op1) {
        if (const llvm::ConstantExpr *CE
                = llvm::dyn_cast<llvm::ConstantExpr>(op)) {
            op1 = createConstantExpr(CE, DL);
        } else {
            llvm::errs() << *op << "\n";
            llvm::errs() << *Inst << "\n";
            assert(0 && "Instruction unspported");
        }
    }

    PSSNode *node = new PSSNode(pss::CAST, op1);
    nodes_map[Inst] = node;

#ifdef DEBUG_ENABLED
    node->setName(getInstName(Inst).c_str());
#endif

    assert(node);
    return node;
}

// return first and last nodes of the block
std::pair<PSSNode *, PSSNode *> buildPSSBlock(const llvm::BasicBlock& block,
                                              const llvm::DataLayout *DL)
{
    using namespace llvm;

    std::pair<PSSNode *, PSSNode *> ret(nullptr, nullptr);
    PSSNode *prev_node;
    PSSNode *node = nullptr;
    for (const Instruction& Inst : block) {
        prev_node = node;

        switch(Inst.getOpcode()) {
            case Instruction::Alloca:
                node = createAlloc(&Inst, DL);
                break;
            case Instruction::Store:
                // create only nodes that store pointer to another
                // pointer. We don't care about stores of non-pointers
                if (Inst.getOperand(0)->getType()->isPointerTy())
                    node = createStore(&Inst, DL);
                break;
            case Instruction::Load:
                if (Inst.getType()->isPointerTy())
                    node = createLoad(&Inst, DL);
                break;
            case Instruction::GetElementPtr:
                node = createGEP(&Inst, DL);
                break;
            case Instruction::BitCast:
                node = createCast(&Inst, DL);
                break;
            case Instruction::Call:
                node = createCall(&Inst);
                break;
        }

        // first instruction
        if (node && !prev_node)
            ret.first = node;

        if (prev_node && prev_node != node)
            prev_node->addSuccessor(node);
    }

    // last node
    ret.second = node;

    return ret;
}

static void handleGlobalVariableInitializer(const llvm::GlobalVariable *GV,
                                            PSSNode *node)
{
    llvm::errs() << *GV << "\n";
    llvm::errs() << "ERROR: ^^^ global variable initializers not implemented\n";
}

std::pair<PSSNode *, PSSNode *> buildGlobals(const llvm::Module *M,
                                             const llvm::DataLayout *DL)
{
    PSSNode *cur = nullptr, *prev, *first = nullptr;
    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        prev = cur;
        llvm::errs() << *I <<"\n";
        // every global node is like memory allocation
        cur = new PSSNode(pss::ALLOC);
        nodes_map[&*I] = cur;

#ifdef DEBUG_ENABLED
        cur->setName(getInstName(&*I).c_str());
#endif

        // handle globals initialization
        const llvm::GlobalVariable *GV
                            = llvm::dyn_cast<llvm::GlobalVariable>(&*I);
        if (GV && GV->hasInitializer() && !GV->isExternallyInitialized())
            handleGlobalVariableInitializer(GV, cur);

        if (prev)
            prev->addSuccessor(cur);
        else
            first = cur;
    }

    assert((!first && !cur) || (first && cur));
    return std::pair<PSSNode *, PSSNode *>(first, cur);
}

} // namespace analysis
} // namespace dg

#endif
