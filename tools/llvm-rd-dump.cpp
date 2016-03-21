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

#include "llvm/LLVMPointsToAnalysis.h"
#include "analysis/PointsToFlowInsensitive.h"
#include "analysis/PointsToFlowSensitive.h"
#include "analysis/Pointer.h"

#include "llvm/LLVMReachingDefinitions.h"

#include "Utils.h"

using namespace dg;
using namespace dg::analysis;
using namespace dg::analysis::rd;
using llvm::errs;

static bool verbose = false;

static void
printName(RDNode *node)
{
    if (node == rd::UNKNOWN_MEMORY) {
        printf("UNKNOWN MEMORY");
        return;
    }

    const char *name = node->getName();
    if (!name) {
        printf("%p\\n", node);
        return;
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
            printName(it.first.target);
            // don't print offsets with unknown memory
            if (it.first.target == rd::UNKNOWN_MEMORY) {
                printf(" => ");
            } else {
                if (it.first.offset.isUnknown())
                    printf(" | UNKNOWN | => ");
                else
                    printf(" | %lu - %lu | => ", *it.first.offset,
                           *it.first.offset + *it.first.len - 1);
            }

            printName(site);
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
        printName(def.target);
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
    printName(n);
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
        printName(node);
        printf("\\n-------------\\n");
        if (verbose) {
            dumpDefines(node, true);
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
dumpPSS(LLVMReachingDefinitions *RD, bool todot)
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
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    llvm::Module *M;
    bool todot = false;
    const char *module = nullptr;
    enum {
        FLOW_SENSITIVE = 1,
        FLOW_INSENSITIVE,
    } type = FLOW_INSENSITIVE;

    // parse options
    for (int i = 1; i < argc; ++i) {
        // run given points-to analysis
        if (strcmp(argv[i], "-pts") == 0) {
            if (strcmp(argv[i+1], "fs") == 0)
                type = FLOW_SENSITIVE;
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

    M = llvm::ParseIRFile(module, SMD, context);
    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    debug::TimeMeasure tm;

    LLVMPointsToAnalysis *PTA;
    if (type == FLOW_INSENSITIVE) {
        PTA = new LLVMPointsToAnalysisImpl<pss::PointsToFlowInsensitive>(M);
    } else {
        PTA = new LLVMPointsToAnalysisImpl<pss::PointsToFlowSensitive>(M);
    }

    tm.start();
    PTA->run();
    tm.stop();
    tm.report("INFO: Points-to analysis took");

    LLVMReachingDefinitions RD(M, PTA);
    tm.start();
    RD.run();
    tm.stop();
    tm.report("INFO: Reaching definitions analysis took");

    dumpPSS(&RD, todot);

    return 0;
}
