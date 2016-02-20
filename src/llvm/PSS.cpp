#ifndef _LLVM_DG_PSS_H_
#define _LLVM_DG_PSS_H_

#include <unordered_map>
#include <cassert>

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
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

static PSSNode *createStore(const llvm::Instruction *Inst)
{
    PSSNode *op1 = nodes_map[Inst->getOperand(0)];
    PSSNode *op2 = nodes_map[Inst->getOperand(1)];

    // the value needs to be a pointer
    assert(Inst->getOperand(0)->getType()->isPointerTy());

    // FIXME
    assert(op1 && "ConstantExpr not supported yet");

    assert(op2 && "Store does not have pointer operand");
    PSSNode *node = new PSSNode(pss::STORE, op1, op2);
    nodes_map[Inst] = node;

#ifdef DEBUG_ENABLED
    node->setName(getInstName(Inst).c_str());
#endif

    assert(node);
    return node;
}

static PSSNode *createLoad(const llvm::Instruction *Inst)
{
    const llvm::Value *op = Inst->getOperand(0);

    PSSNode *op1 = nodes_map[op];
    // FIXME
    assert(op1 && "ConstantExpr not supported yet");

    PSSNode *node = new PSSNode(pss::LOAD, op1);
    nodes_map[Inst] = node;

#ifdef DEBUG_ENABLED
    node->setName(getInstName(Inst).c_str());
#endif

    assert(node);
    return node;
}

static inline unsigned getPointerBitwidth(const llvm::DataLayout *DL,
                                          const llvm::Value *ptr)
{
    const llvm::Type *Ty = ptr->getType();
    return DL->getPointerSizeInBits(Ty->getPointerAddressSpace());
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
    // FIXME
    assert(op && "ConstantExpr not supported yet");

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
                    node = createStore(&Inst);
                break;
            case Instruction::Load:
                if (Inst.getType()->isPointerTy())
                    node = createLoad(&Inst);
                break;
            case Instruction::GetElementPtr:
                node = createGEP(&Inst, DL);
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


} // namespace analysis
} // namespace dg

#endif
