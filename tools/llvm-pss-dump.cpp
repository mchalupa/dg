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

#include "Utils.h"

using namespace dg;
using llvm::errs;
using analysis::PointsToFlowInsensitive;
using analysis::PointsToFlowSensitive;
using analysis::PSSNode;

static void
dumpPSSNode(PSSNode *n)
{
    const char *name = n->getName();

    if (name)
        printf("%s", name);
    else
        printf("<%p>", n);

    if (n->getSize() || n->isHeap() || n->isZeroInitialized())
        printf(" [size: %lu, heap: %u, zeroed: %u]\n",
               n->getSize(), n->isHeap(), n->isZeroInitialized());
    else
        putchar('\n');

    for (const analysis::Pointer& ptr : n->pointsTo) {
        printf("    -> %s + ", ptr.target->getName());
        if (ptr.offset.isUnknown())
            puts("UNKNOWN_OFFSET");
        else
            printf("%lu\n", *ptr.offset);
    }
}

static void
dumpPSSdot(analysis::PSS *pss)
{
    std::set<PSSNode *> nodes;
    pss->getNodes(nodes);

    printf("digraph \"Pointer State Subgraph\" {\n");

    /* dump nodes */
    for (PSSNode *node : nodes) {
        printf("\tNODE%p [label=\"", node);
        const char *name = node->getName();
        if (name)
            printf("%s\\n", name);
        else
            printf("%p\\n", node);

        if (node->getSize() || node->isHeap() || node->isZeroInitialized())
            printf("size: %lu, heap: %u, zeroed: %u\\n",
               node->getSize(), node->isHeap(), node->isZeroInitialized());

        for (const analysis::Pointer& ptr : node->pointsTo) {
            printf("    -> %s + ", ptr.target->getName());
            if (ptr.offset.isUnknown())
                printf("UNKNOWN_OFFSET\\n");
            else
                printf("%lu\\n", *ptr.offset);
        }

        printf("\"");
        if (node->getType() != analysis::pss::STORE) {
            printf(" shape=box");
            if (node->pointsTo.size() == 0)
                printf("fillcolor=red");
        } else {
            printf(" shape=cds");
        }

        printf("]\n");
    }

    /* dump edges */
    for (PSSNode *node : nodes) {
        for (PSSNode *succ : node->getSuccessors())
            printf("\tNODE%p -> NODE%p [penwidth=2]\n", node, succ);
    }

    printf("}\n");
}

static void
dumpPSS(analysis::PSS *pss, bool todot)
{
    assert(pss);

    if (todot)
        dumpPSSdot(pss);
    else {
        std::set<PSSNode *> nodes;
        pss->getNodes(nodes);

        for (PSSNode *node : nodes) {
            dumpPSSNode(node);
        }
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
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module [output_file]\n";
        return 1;
    }

    M = llvm::ParseIRFile(module, SMD, context);
    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    debug::TimeMeasure tm;

    if (type == FLOW_INSENSITIVE) {
        LLVMPointsToAnalysis<PointsToFlowInsensitive> PTA(M);

        tm.start();
        PTA.run();
        tm.stop();
        tm.report("INFO: Points-to analysis took");

        dumpPSS(PTA.getPSS(), todot);
    } else {
        LLVMPointsToAnalysis<PointsToFlowSensitive> PTA(M);

        tm.start();
        PTA.run();
        tm.stop();
        tm.report("INFO: Points-to analysis took");

        dumpPSS(PTA.getPSS(), todot);
    }

    return 0;
}
