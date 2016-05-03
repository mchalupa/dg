#include <assert.h>
#include <cstdio>

#include <set>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Bitcode/ReaderWriter.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>

#include "llvm/analysis/PointsTo.h"

#include "analysis/PointsTo/PointsToFlowInsensitive.h"
#include "analysis/PointsTo/PointsToFlowSensitive.h"
#include "analysis/PointsTo/Pointer.h"

#include "Utils.h"

using namespace dg;
using namespace dg::analysis::pss;
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
printName(PSSNode *node)
{
    const char *name = node->getName();
    std::string nm;
    if (!name) {
        if (!node->getUserData<llvm::Value>()) {
            printf("%p\n", node);

            return;
        }

        nm = getInstName(node->getUserData<llvm::Value>());
        name = nm.c_str();
    }

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
dumpPSSNode(PSSNode *n)
{
    const char *name = n->getName();

    printf("NODE: ");
    printName(n);

    if (n->getSize() || n->isHeap() || n->isZeroInitialized())
        printf(" [size: %lu, heap: %u, zeroed: %u]",
               n->getSize(), n->isHeap(), n->isZeroInitialized());

    if (n->pointsTo.empty()) {
        puts(" -- no points-to");
        return;
    } else
        putchar('\n');

    for (const Pointer& ptr : n->pointsTo) {
        printf("    -> ");
        printName(ptr.target);
        if (ptr.offset.isUnknown())
            puts(" + UNKNOWN_OFFSET");
        else
            printf(" + %lu\n", *ptr.offset);
    }
}

static bool verify_ptsets(LLVMPointsToAnalysis *fi, LLVMPointsToAnalysis *fs)
{
    bool ok = true;
    const std::unordered_map<const llvm::Value *, PSSNode *>& mp = fi->getNodesMap();
    for (auto it : mp) {
        PSSNode *fsnode = fs->getPointsTo(it.first);
        if (!fsnode) {
            llvm::errs() << "FS don't have points-to for: " << *it.first << "\n"
                         << "but FI has:\n";
            dumpPSSNode(it.second);
            ok = false;
            continue;
        }

        if (!it.second) {
            llvm::errs() << "PSS node is null: " << *it.first << "\n";
            ok = false;
            continue;
        }

        for (const Pointer& ptr : fsnode->pointsTo) {
            bool found = false;
            for (const Pointer& ptr2 : it.second->pointsTo) {
                // either the pointer is there or
                // FS has (target, offset) and FI has (target, UNKNOWN_OFFSET),
                // than everything is fine. The other case (FS has UNKNOWN_OFFSET)
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
                    llvm::errs() << "FS not subset of FI: " << *it.first << "\n";
                    llvm::errs() << "FI:\n";
                    dumpPSSNode(it.second);
                    llvm::errs() << "FS:\n";
                    dumpPSSNode(fsnode);
                    llvm::errs() << " ---- \n";
                    ok = false;
            }
        }
    }

    return ok;
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
        errs() << "Usage: % llvm-pss-compare [-pta fs|fi] IR_module\n";
        return 1;
    }

#if (LLVM_VERSION_MINOR < 5)
    M = llvm::ParseIRFile(module, SMD, context);
#else
    auto _M = llvm::parseIRFile(module, SMD, context);
    // _M is unique pointer, we need to get Module *
    M = &*_M;
#endif

    debug::TimeMeasure tm;

    LLVMPointsToAnalysis *PTAfs;
    LLVMPointsToAnalysis *PTAfi;
    if (type & FLOW_INSENSITIVE) {
        PTAfi = new LLVMPointsToAnalysisImpl<analysis::pss::PointsToFlowInsensitive>(M);

        tm.start();
        PTAfi->run();
        tm.stop();
        tm.report("INFO: Points-to flow-insensitive analysis took");
    }

    if (type & FLOW_SENSITIVE) {
            PTAfs = new LLVMPointsToAnalysisImpl<analysis::pss::PointsToFlowSensitive>(M);
        tm.start();
        PTAfs->run();
        tm.stop();
        tm.report("INFO: Points-to flow-sensitive analysis took");
    }

    int ret = 0;
    if (type == (FLOW_SENSITIVE | FLOW_INSENSITIVE)) {
        ret = !verify_ptsets(PTAfi, PTAfs);
        if (ret == 0)
            llvm::errs() << "FS is a subset of FI, all OK\n";
    }

    return ret;
}
