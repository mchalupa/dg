#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <set>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "llvm/analysis/PointsTo/PointsTo.h"

#include "analysis/PointsTo/PointsToFlowInsensitive.h"
#include "analysis/PointsTo/PointsToFlowSensitive.h"
#include "analysis/PointsTo/Pointer.h"

#include "TimeMeasure.h"

using namespace dg;
using namespace dg::analysis::pta;
using llvm::errs;

enum PTType {
    FLOW_SENSITIVE = 1,
    FLOW_INSENSITIVE,
};

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

static void
printName(PSNode *node)
{
    if (!node->getUserData<llvm::Value>()) {
        printf("%p", static_cast<void*>(node));
        return;
    }

    std::string nm = getInstName(node->getUserData<llvm::Value>());
    const char *name = nm.c_str();

    // escape the " character
    for (int i = 0; name[i] != '\0'; ++i) {
        // crop long names
        if (i >= 70) {
            printf(" ...");
            break;
        }

        if (name[i] == '"')
            putchar('\\');

        putchar(name[i]);
    }
}

static void
dumpPSNode(PSNode *n)
{
    printf("NODE %3u: ", n->getID());
    printName(n);

    PSNodeAlloc *alloc = PSNodeAlloc::get(n);
    if (alloc &&
        (alloc->getSize() || alloc->isHeap() || alloc->isZeroInitialized()))
        printf(" [size: %lu, heap: %u, zeroed: %u]",
               alloc->getSize(), alloc->isHeap(), alloc->isZeroInitialized());

    if (n->pointsTo.empty()) {
        puts("\n    -> no points-to");
        return;
    } else
        putchar('\n');

    for (const Pointer& ptr : n->pointsTo) {
        printf("    -> ");
        printName(ptr.target);
        if (ptr.offset.isUnknown())
            puts(" + Offset::UNKNOWN");
        else
            printf(" + %lu\n", *ptr.offset);
    }
}

static bool verify_ptsets(const llvm::Value *val,
                          LLVMPointerAnalysis *fi,
                          LLVMPointerAnalysis *fs)
{
    PSNode *finode = fi->getPointsTo(val);
    PSNode *fsnode = fs->getPointsTo(val);

    if (!finode) {
        if (fsnode) {
            llvm::errs() << "FI don't have points-to for: " << *val << "\n"
                         << "but FS has:\n";
            dumpPSNode(fsnode);
        } else
            // if boths mapping are null we assume that
            // the value is not reachable from main
            // (if nothing more, its not different for FI and FS)
            return true;

        return false;
    }

    if (!fsnode) {
        if (finode) {
            llvm::errs() << "FS don't have points-to for: " << *val << "\n"
                         << "but FI has:\n";
            dumpPSNode(finode);
        } else
            return true;

        return false;
    }

    for (const Pointer& ptr : fsnode->pointsTo) {
        bool found = false;
        for (const Pointer& ptr2 : finode->pointsTo) {
            // either the pointer is there or
            // FS has (target, offset) and FI has (target, Offset::UNKNOWN),
            // than everything is fine. The other case (FS has Offset::UNKNOWN)
            // we don't consider here, since that should not happen
            if ((ptr2.target->getUserData<llvm::Value>()
                == ptr.target->getUserData<llvm::Value>())
                && (ptr2.offset == ptr.offset ||
                    ptr2.offset.isUnknown()
                    /* || ptr.offset.isUnknown()*/)) {
                found = true;
                break;
            }
        }

        if (!found) {
                llvm::errs() << "FS not subset of FI: " << *val << "\n";
                llvm::errs() << "FI ";
                dumpPSNode(finode);
                llvm::errs() << "FS ";
                dumpPSNode(fsnode);
                llvm::errs() << " ---- \n";
                return false;
        }
    }

    return true;
}

static bool verify_ptsets(llvm::Module *M,
                          LLVMPointerAnalysis *fi,
                          LLVMPointerAnalysis *fs)
{
    using namespace llvm;
    bool ret = true;

    for (Function& F : *M)
        for (BasicBlock& B : F)
            for (Instruction& I : B)
                if (!verify_ptsets(&I, fi, fs))
                    ret = false;

    return ret;
}

int main(int argc, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    const char *module = nullptr;
    unsigned type = FLOW_SENSITIVE | FLOW_INSENSITIVE;

    // parse options
    for (int i = 1; i < argc; ++i) {
        // run only given points-to analysis
        if (strcmp(argv[i], "-pta") == 0) {
            if (strcmp(argv[i+1], "fs") == 0)
                type = FLOW_SENSITIVE;
            else if (strcmp(argv[i+1], "fi") == 0)
                type = FLOW_INSENSITIVE;
            else {
                errs() << "Unknown PTA type" << argv[i + 1] << "\n";
                abort();
            }
        /*} else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;*/
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % llvm-pta-compare [-pta fs|fi] IR_module\n";
        return 1;
    }

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 5))
    M = llvm::ParseIRFile(module, SMD, context);
#else
    auto _M = llvm::parseIRFile(module, SMD, context);
    // _M is unique pointer, we need to get Module *
    M = _M.get();
#endif

    if (!M) {
        llvm::errs() << "Failed parsing '" << module << "' file:\n";
        SMD.print(argv[0], errs());
        return 1;
    }

    dg::debug::TimeMeasure tm;

    LLVMPointerAnalysis *PTAfs = nullptr;
    LLVMPointerAnalysis *PTAfi = nullptr;

    if (type & FLOW_INSENSITIVE) {
        PTAfi = new LLVMPointerAnalysis(M);

        tm.start();
        PTAfi->run<analysis::pta::PointsToFlowInsensitive>();
        tm.stop();
        tm.report("INFO: Points-to flow-insensitive analysis took");
    }

    if (type & FLOW_SENSITIVE) {
        PTAfs = new LLVMPointerAnalysis(M);

        tm.start();
        PTAfs->run<analysis::pta::PointsToFlowSensitive>();
        tm.stop();
        tm.report("INFO: Points-to flow-sensitive analysis took");
    }

    int ret = 0;
    if (type == (FLOW_SENSITIVE | FLOW_INSENSITIVE)) {
        ret = !verify_ptsets(M, PTAfi, PTAfs);
        if (ret == 0)
            llvm::errs() << "FS is a subset of FI, all OK\n";
    }

    delete PTAfi;
    delete PTAfs;

    return ret;
}
