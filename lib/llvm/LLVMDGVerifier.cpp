#include <cstdarg>
#include <cstdio>

#include <llvm/IR/Function.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMNode.h"
#include "llvm/LLVMDGVerifier.h"

namespace dg {

void LLVMDGVerifier::fault(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "ERR dg-verify: ");
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);

    fflush(stderr);

    ++faults;
}

bool LLVMDGVerifier::verify() {
    checkMainProc();

    extern std::map<llvm::Value *, LLVMDependenceGraph *> constructedFunctions;
    for (auto &it : constructedFunctions)
        checkGraph(llvm::cast<llvm::Function>(it.first), it.second);

    fflush(stderr);
    return faults == 0;
}

void LLVMDGVerifier::checkMainProc() {
    if (!dg->module)
        fault("has no module set");

    // all the subgraphs must have the same global nodes

    for (const auto &it : getConstructedFunctions()) {
        if (it.second->global_nodes != dg->global_nodes)
            fault("subgraph has different global nodes than main proc");
    }
}

void LLVMDGVerifier::checkNode(const llvm::Value *val, LLVMNode *node) {
    if (!node->getBBlock()) {
        fault("node has no value set");
        llvm::errs() << "  -> " << *val << "\n";
    }

    // FIXME if this is a call-size, check that the parameters match
}

void LLVMDGVerifier::checkBBlock(const llvm::BasicBlock *llvmBB,
                                 LLVMBBlock *BB) {
    using namespace llvm;
    auto BBIT = BB->getNodes().begin();

    for (const Instruction &I : *llvmBB) {
        LLVMNode *node = *BBIT;

        // check if we have the CFG edges set
        if (node->getKey() != &I)
            fault("wrong node in BB");

        checkNode(&I, node);
        ++BBIT;
    }

    // FIXME: check successors and predecessors
}

void LLVMDGVerifier::checkGraph(llvm::Function *F, LLVMDependenceGraph *g) {
    using namespace llvm;

    LLVMNode *entry = g->getEntry();
    if (!entry) {
        fault("has no entry for %s", F->getName().data());
        return;
    }

    const llvm::Function *func = dyn_cast<Function>(entry->getKey());
    if (!func) {
        fault("key in entry node is not a llvm::Function");
        return;
    }

    size_t a, b;
    a = g->getBlocks().size();
    b = func->size();
    if (a != b)
        fault("have constructed %lu BBlocks but function has %lu basic blocks",
              a, b);

    for (BasicBlock &llvmBB : *F) {
        LLVMBBlock *BB = g->getBlocks()[&llvmBB];
        if (!BB) {
            fault("missing BasicBlock");
            errs() << llvmBB << "\n";
        } else
            checkBBlock(&llvmBB, BB);
    }
}

}; // namespace dg
