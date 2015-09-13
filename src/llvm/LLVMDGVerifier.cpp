#include <cstdio>
#include <cstdarg>

#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include "LLVMDependenceGraph.h"
#include "LLVMNode.h"
#include "LLVMDGVerifier.h"

namespace dg {

void LLVMDGVerifier::fault(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "ERR dg-verify: ");
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);

    fflush(stderr);

    ++faults;
}

bool LLVMDGVerifier::verify()
{
    checkMainProc();
    for (auto it : dg->constructedFunctions)
        checkGraph(llvm::cast<llvm::Function>(it.first), it.second);

    fflush(stderr);
    return faults == 0;
}

void LLVMDGVerifier::checkMainProc()
{
    if (!dg->module)
        fault("has no module set");
}

void LLVMDGVerifier::checkNode(const llvm::Value *val, LLVMNode *node)
{
    if (!node->getBasicBlock()) {
        fault("node has no value set");
        llvm::errs() << "  -> " << *val << "\n";
    }
}

void LLVMDGVerifier::checkBBlock(const llvm::BasicBlock *llvmBB, LLVMBBlock *BB)
{
    using namespace llvm;
    LLVMNode *node = BB->getFirstNode();
    const llvm::Value *llvmPrev = nullptr;
    for (const Instruction& I : *llvmBB) {
        // check if we have the CFG edges set
        if (node->getKey() != &I)
            fault("wrong node in BB");

        if (llvmPrev) {
            if (llvmPrev != node->getPredcessor()->getKey())
                fault("predcessor edges is wrong");
        }

        checkNode(&I, node);
        llvmPrev = &I;
        node = node->getSuccessor();
    }
}

void LLVMDGVerifier::checkGraph(const llvm::Function *F, LLVMDependenceGraph *g)
{
    using namespace llvm;

    LLVMNode *entry = g->getEntry();
    if (!entry)
        fault("has no entry for %s", F->getName().data());

    const llvm::Function *func = dyn_cast<Function>(entry->getKey());
    if (!func)
        fault("key in entry node is not a llvm::Function");

    size_t a, b;
    a = g->constructedBlocks.size();
    b = func->size();
    if (a != b)
        fault("have constructed %lu BBlocks but function has %lu basic blocks", a, b);

    for (const BasicBlock& llvmBB : *F) {
        LLVMBBlock *BB = g->constructedBlocks[&llvmBB];
        if (!BB) {
            fault("missing BasicBlock");
            errs() << llvmBB << "\n";
        } else
            checkBBlock(&llvmBB, BB);
    }
}

};
