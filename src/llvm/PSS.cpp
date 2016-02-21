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


Pointer getConstantExprPointer(const llvm::ConstantExpr *CE,
                               const llvm::DataLayout *DL);

static inline unsigned getPointerBitwidth(const llvm::DataLayout *DL,
                                          const llvm::Value *ptr)

{
    const llvm::Type *Ty = ptr->getType();
    return DL->getPointerSizeInBits(Ty->getPointerAddressSpace());
}

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

static PSSNode *createAlloc(const llvm::Instruction *Inst)
{
    PSSNode *node = new PSSNode(pss::ALLOC);
    nodes_map[Inst] = node;

#ifdef DEBUG_ENABLED
    node->setName(getInstName(Inst).c_str());
#endif

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
                node = createAlloc(&Inst);
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
