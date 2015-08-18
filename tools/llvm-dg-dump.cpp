#include <assert.h>
#include <cstdio>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>

#include <iostream>
#include "llvm/LLVMDependenceGraph.h"
#include "DG2Dot.h"

using namespace dg;
using llvm::errs;

static std::ostream& printLLVMVal(std::ostream& os, const llvm::Value *val)
{
    llvm::raw_os_ostream ro(os);

    if (!val) {
        ro << "(null)";
        return os;
    }

    if (llvm::isa<llvm::Function>(val))
        ro << "ENTRY " << val->getName();
    else
        // just dump it there
        ro << *val;

    return os;
}

static bool checkNode(std::ostream& os, LLVMNode *node)
{
    bool err = false;
    const llvm::Value *val = node->getKey();

    if (!val) {
        os << "\\nERR: no value in node";
        return true;
    }

    if (!node->getBasicBlock()
        && !llvm::isa<llvm::Function>(val)) {
        err = true;
        os << "\\nERR: no BB";
    }

    LLVMNode *s = node->getSuccessor();
    LLVMNode *p = node->getPredcessor();
    if (s) {
        if (s->getPredcessor() != node) {
            os << "\\nERR: wrong predcessor";
            err = true;
        }

        if(s->getBasicBlock() != node->getBasicBlock()) {
            os << "\\nERR: succ BB mismatch";
            err = true;
        }
    }

    if (p) {
        if (p->getSuccessor() != node) {
            os << "\\nERR: wrong successor";
            err = true;
        }

        if(p->getBasicBlock() != node->getBasicBlock()) {
            os << "\\nERR: pred BB mismatch";
            err = true;
        }
    }

    return err;
}

int main(int argc, char *argv[])
{
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    std::unique_ptr<llvm::Module> M;
    const char *module = NULL;

    using namespace debug;
    uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-no-control") == 0) {
            opts &= ~PRINT_CD;
        } else if (strcmp(argv[i], "-no-data") == 0) {
            opts &= ~PRINT_DD;
        } else if (strcmp(argv[i], "-cfg") == 0) {
            opts |= PRINT_CFG;
        } else if (strcmp(argv[i], "-call") == 0) {
            opts |= PRINT_CALL;
        } else if (strcmp(argv[i], "-cfgall") == 0) {
            opts |= PRINT_CFG;
            opts |= PRINT_REV_CFG;
        } else if (strcmp(argv[i], "-pss") == 0) {
            opts |= PRINT_PSS;
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module [output_file]\n";
        return 1;
    }

    M = llvm::parseIRFile(module, SMD, context);
    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    LLVMDependenceGraph d;
    d.build(&*M);

    debug::DG2Dot<LLVMNode> dump(&d, opts);

    dump.printKey = printLLVMVal;
    dump.checkNode = checkNode;

    dump.dump("/dev/stdout", d.getEntryBB());

    return 0;
}
