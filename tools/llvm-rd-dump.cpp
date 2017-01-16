#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <set>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cassert>
#include <cstdio>

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
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "analysis/PointsTo/PointsToFlowInsensitive.h"
#include "analysis/PointsTo/PointsToFlowSensitive.h"
#include "analysis/PointsTo/Pointer.h"

#include "llvm/analysis/PointsTo/PointsTo.h"
#include "llvm/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "TimeMeasure.h"

using namespace dg;
using namespace dg::analysis;
using namespace dg::analysis::rd;
using llvm::errs;

static bool verbose = false;

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
printName(RDNode *node, bool dot)
{
    if (node == rd::UNKNOWN_MEMORY) {
        printf("UNKNOWN MEMORY");
        return;
    }

    const char *name = nullptr;
#if 0
#ifdef DEBUG_ENABLED
    name = node->getName();
#endif
#endif
    std::string nm;
    if (!name) {
        if (!node->getUserData<llvm::Value>()) {
            if (dot)
                printf("%p\\n", node);
            else
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
dumpMap(RDNode *node, bool dot = false)
{
    RDMap& map = node->getReachingDefinitions();
    for (auto it : map) {
        for (RDNode *site : it.second) {
            printName(it.first.target, dot);
            // don't print offsets with unknown memory
            if (it.first.target == rd::UNKNOWN_MEMORY) {
                printf(" => ");
            } else {
                if (it.first.offset.isUnknown())
                    printf(" | UNKNOWN | => ");
                else if (it.first.len.isUnknown())
                    printf(" | %lu - UNKNOWN | => ", *it.first.offset);
                else
                    printf(" | %lu - %lu | => ", *it.first.offset,
                           *it.first.offset + *it.first.len - 1);
            }

            printName(site, dot);
            if (dot)
                printf("\\n");
            else
                putchar('\n');
        }
    }
}

static void
dumpDefines(RDNode *node, bool dot = false)
{
    for (const DefSite& def : node->getDefines()) {
        printf("DEF: ");
        printName(def.target, dot);
            if (def.offset.isUnknown())
                printf(" [ UNKNOWN ]");
            else
                printf(" [ %lu - %lu ]", *def.offset,
                       *def.offset + *def.len - 1);

            if (dot)
                printf("\\n");
            else
                putchar('\n');
    }
}

static void
dumpOverwrites(RDNode *node, bool dot = false)
{
    for (const DefSite& def : node->getOverwrites()) {
        printf("DEF strong: ");
        printName(def.target, dot);
            if (def.offset.isUnknown())
                printf(" [ UNKNOWN ]");
            else
                printf(" [ %lu - %lu ]", *def.offset,
                       *def.offset + *def.len - 1);

            if (dot)
                printf("\\n");
            else
                putchar('\n');
    }
}


static void
dumpRDNode(RDNode *n)
{
    printf("NODE: ");
    printName(n, false);
    if (n->getSize() > 0)
        printf(" [size: %lu]", n->getSize());
    putchar('\n');
    dumpMap(n);
    printf("---\n");
}

static void
dumpRDdot(LLVMReachingDefinitions *RD)
{
    std::set<RDNode *> nodes;
    RD->getNodes(nodes);

    printf("digraph \"Pointer State Subgraph\" {\n");

    /* dump nodes */
    for(RDNode *node : nodes) {
        printf("\tNODE%p [label=\"", node);
        printName(node, true);
        if (node->getSize() > 0)
            printf("\\n[size: %lu]\\n", node->getSize());
        printf("\\n-------------\\n");
        if (verbose) {
            dumpDefines(node, true);
            printf("-------------\\n");
            dumpOverwrites(node, true);
            printf("-------------\\n");
        }
            dumpMap(node, true /* dot */);

        printf("\" shape=box]\n");
    }

    /* dump edges */
    for (RDNode *node : nodes) {
        for (RDNode *succ : node->getSuccessors())
            printf("\tNODE%p -> NODE%p [penwidth=2]\n", node, succ);
    }

    printf("}\n");
}

static void
dumpRD(LLVMReachingDefinitions *RD, bool todot)
{
    assert(RD);

    if (todot)
        dumpRDdot(RD);
    else {
        std::set<RDNode *> nodes;
        RD->getNodes(nodes);

        for (RDNode *node : nodes)
            dumpRDNode(node);
    }
}

int main(int argc, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    bool todot = false;
    const char *module = nullptr;
    uint64_t field_senitivity = UNKNOWN_OFFSET;
    bool rd_strong_update_unknown = false;
    uint32_t max_set_size = ~((uint32_t) 0);

    enum {
        FLOW_SENSITIVE = 1,
        FLOW_INSENSITIVE,
    } type = FLOW_INSENSITIVE;

    // parse options
    for (int i = 1; i < argc; ++i) {
        // run given points-to analysis
        if (strcmp(argv[i], "-pta") == 0) {
            if (strcmp(argv[i+1], "fs") == 0)
                type = FLOW_SENSITIVE;
        } else if (strcmp(argv[i], "-pta-field-sensitive") == 0) {
            field_senitivity = (uint64_t) atoll(argv[i + 1]);
        } else if (strcmp(argv[i], "-rd-max-set-size") == 0) {
            max_set_size = (uint64_t) atoll(argv[i + 1]);
            if (max_set_size == 0) {
                llvm::errs() << "Invalid -rd-max-set-size argument\n";
                abort();
            }
        } else if (strcmp(argv[i], "-rd-strong-update-unknown") == 0) {
            rd_strong_update_unknown = true;
        } else if (strcmp(argv[i], "-dot") == 0) {
            todot = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module [-pts fs|fi] [-dot] [-v] [output_file]\n";
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

    debug::TimeMeasure tm;

    LLVMPointerAnalysis PTA(M, field_senitivity);

    tm.start();

    if (type == FLOW_INSENSITIVE) {
        PTA.run<pta::PointsToFlowInsensitive>();
    } else {
        PTA.run<pta::PointsToFlowSensitive>();
    }

    tm.stop();
    tm.report("INFO: Points-to analysis took");

    LLVMReachingDefinitions RD(M, &PTA, rd_strong_update_unknown, max_set_size);
    tm.start();
    RD.run();
    tm.stop();
    tm.report("INFO: Reaching definitions analysis took");

    dumpRD(&RD, todot);

    return 0;
}
