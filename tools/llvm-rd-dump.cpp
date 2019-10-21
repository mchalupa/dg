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

#include "dg/analysis/PointsTo/PointerAnalysisFI.h"
#include "dg/analysis/PointsTo/PointerAnalysisFS.h"
#include "dg/analysis/PointsTo/Pointer.h"

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"
#include "dg/llvm/analysis/DataDependence/DataDependence.h"

#include "TimeMeasure.h"

using namespace dg;
using namespace dg::analysis;
using llvm::errs;

static bool verbose = false;
static const char *entryFunc = "main";

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

static void printRWNodeType(enum RWNodeType type)
{
#define ELEM(t) case(t): do {printf("%s", #t); }while(0); break;
    switch(type) {
        ELEM(RWNodeType::ALLOC)
        ELEM(RWNodeType::DYN_ALLOC)
        ELEM(RWNodeType::STORE)
        ELEM(RWNodeType::PHI)
        ELEM(RWNodeType::CALL)
        ELEM(RWNodeType::CALL_RETURN)
        ELEM(RWNodeType::FORK)
        ELEM(RWNodeType::JOIN)
        ELEM(RWNodeType::RETURN)
        ELEM(RWNodeType::NOOP)
        ELEM(RWNodeType::NONE)
        default:
            printf("unknown reaching definitions subgraph type");
    };
#undef ELEM
}

static inline void printAddress(RWNode *node, bool dot)
{
    if (dot)
        printf(" [%p]\\n", static_cast<void*>(node));
    else
        printf(" [%p]\n", static_cast<void*>(node));
}

static void
printName(RWNode *node, bool dot)
{
    if (node == UNKNOWN_MEMORY) {
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
            printRWNodeType(node->getType());
            printAddress(node, dot);
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
dumpMap(RWNode *node, bool dot = false)
{
    RDMap& map = node->def_map;
    for (const auto& it : map) {
        for (RWNode *site : it.second) {
            printName(it.first.target, dot);
            // don't print offsets with unknown memory
            if (it.first.target == UNKNOWN_MEMORY) {
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

template <typename T>
static void printInterval(T& I, const char *pref = nullptr,
                          const char *suff = nullptr) {
    if (pref)
        printf("%s", pref);

    if (I.start.isUnknown())
        printf("[? - ");
    else
        printf("[%lu - ", *I.start);

    if (I.end.isUnknown())
        printf("?]");
    else
        printf("%lu]", *I.end);

    if (suff)
        printf("%s", suff);
}

static void
dumpDDIMap(RWBBlock *block, bool dot = false)
{
    auto& map = block->definitions;
    for (const auto& it : map) {
       printf("\\l----  ");
       printName(it.first, dot);
       printf("  ----\\l");
       for (auto& it2 : it.second) {
           printInterval(it2.first, "  ", " => \\l");
           for (auto where : it2.second) {
               printf("      ");
               printName(where, dot);
               if (dot)
                   printf("\\n");
               else
                   putchar('\n');
           }
       }
    }
}

static void
dumpDefSites(const std::set<DefSite>& defs, const char *kind, bool dot = false)
{
    printf("-------------\\n");
    for (const DefSite& def : defs) {
        printf("%s: ", kind);
        printName(def.target, dot);
            if (def.offset.isUnknown())
                printf(" [? - ");
            else
                printf(" [%lu - ", *def.offset);

            if (def.len.isUnknown())
                printf("?]");
            else
                printf("%lu]", *def.offset + (*def.len - 1));

            if (dot)
                printf("\\n");
            else
                putchar('\n');
    }
}

static void
dumpDefines(RWNode *node, bool dot = false)
{
    if (!node->getDefines().empty())
        dumpDefSites(node->getDefines(), "DEF", dot);
}


static void
dumpOverwrites(RWNode *node, bool dot = false)
{
    if (!node->getOverwrites().empty())
        dumpDefSites(node->getOverwrites(), "DEF strong", dot);
}

static void
dumpUses(RWNode *node, bool dot = false)
{
    if (!node->getUses().empty())
        dumpDefSites(node->getUses(), "USE", dot);
}

static void
dumpRWNode(RWNode *n)
{
    printf("NODE: ");
    printName(n, false);
    if (n->getSize() > 0)
        printf(" [size: %lu]", n->getSize());
    putchar('\n');
    dumpMap(n);
    printf("---\n");
}

static void nodeToDot(RWNode *node) {
    printf("\tNODE%p [label=\"%u ", static_cast<void*>(node), node->getID());
    printName(node, true);
    if (node->getSize() > 0) {
        printf("\\n[size: %lu]\\n", node->getSize());
        printf("\\n-------------\\n");
    }

    if (verbose) {
        printf("\\nblock: %p\\n", node->getBBlock());
        printf("\\n-------------\\n");

        dumpDefines(node, true);
        dumpOverwrites(node, true);
        dumpUses(node, true);
        printf("\\n-------------\\n");
    }

    dumpMap(node, true /* dot */);

    printf("\" shape=box]\n");

}

static void dumpDotOnlyNodes(LLVMDataDependenceAnalysis *RD)
{
    const auto& nodes = RD->getNodes();
    // dump nodes
    for(RWNode *node : nodes) {
        nodeToDot(node);
    }

    // dump def-use edges
    for(RWNode *node : nodes) {
        if (node->isUse()) {
            for (RWNode *def : RD->getDefinitions(node)) {
                printf("\tNODE%p->NODE%p [style=dotted]",
                       static_cast<void*>(def), static_cast<void*>(node));
            }
        }
    }
}

static void dumpDotWithBlocks(LLVMDataDependenceAnalysis *RD) {

    for (auto I = RD->getGraph()->blocks_begin(),
              E = RD->getGraph()->blocks_end(); I != E; ++I) {
        printf("subgraph cluster_%p {\n", *I);
        /* dump nodes */
        for(RWNode *node : I->getNodes()) {
            nodeToDot(node);
        }

        // dump def-use edges
        for(RWNode *node : I->getNodes()) {
            for (RWNode *def : node->defuse) {
                printf("\tNODE%p->NODE%p [style=dotted]",
                       static_cast<void*>(def), static_cast<void*>(node));
            }
            if (node->isUse()) {
                for (RWNode *def : RD->getDefinitions(node)) {
                    printf("\tNODE%p->NODE%p [style=dotted color=blue]",
                           static_cast<void*>(def), static_cast<void*>(node));
                }
            }
        }
        printf("label=\"\\nblock: %p\\n", *I);
        dumpDDIMap(*I, true);
        printf("\"\nlabelloc=b\n");
        printf("}\n");
    }

    /* dump block edges
    for (auto I = RD->getGraph()->blocks_begin(),
              E = RD->getGraph()->blocks_end(); I != E; ++I) {
        for (auto succ : I->getSuccessors())
            printf("\tNODE%p -> NODE%p [penwidth=2]\n",
                   static_cast<void*>(*I),
                   static_cast<void*>(succ));
    }
    */
}

static void
dumpDefsToDot(LLVMDataDependenceAnalysis *RD)
{

    printf("digraph \"Reaching Definitions Subgraph\" {\n");

    if (RD->getGraph()->getBBlocks().empty())
        dumpDotOnlyNodes(RD);
    else
        dumpDotWithBlocks(RD);

    /* dump edges */
    for (RWNode *node : RD->getNodes()) {
        for (RWNode *succ : node->getSuccessors())
            printf("\tNODE%p -> NODE%p [penwidth=2]\n",
                   static_cast<void*>(node),
                   static_cast<void*>(succ));
    }

    printf("}\n");
}

static void
dumpDefs(LLVMDataDependenceAnalysis *RD, bool todot)
{
    assert(RD);

    if (todot)
        dumpDefsToDot(RD);
    else {
        for (RWNode *node : RD->getNodes())
            dumpRWNode(node);
    }
}

int main(int argc, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    bool todot = false;
    bool threads = false;
    const char *module = nullptr;
    Offset::type field_sensitivity = Offset::UNKNOWN;
    bool rd_strong_update_unknown = false;
    Offset::type max_set_size = Offset::UNKNOWN;

    enum {
        FLOW_SENSITIVE = 1,
        FLOW_INSENSITIVE,
    } type = FLOW_INSENSITIVE;

    enum class RdaType {
        DATAFLOW,
        SSA
    } rda = RdaType::DATAFLOW;

    // parse options
    for (int i = 1; i < argc; ++i) {
        // run given points-to analysis
        if (strcmp(argv[i], "-pta") == 0) {
            if (strcmp(argv[i+1], "fs") == 0)
                type = FLOW_SENSITIVE;
        } else if (strcmp(argv[i], "-dda") == 0) {
            if (strcmp(argv[i+1], "ssa") == 0)
                rda = RdaType::SSA;
        } else if (strcmp(argv[i], "-pta-field-sensitive") == 0) {
            field_sensitivity = static_cast<Offset::type>(atoll(argv[i + 1]));
        } else if (strcmp(argv[i], "-rd-max-set-size") == 0) {
            max_set_size = static_cast<Offset::type>(atoll(argv[i + 1]));
            if (max_set_size == 0) {
                llvm::errs() << "Invalid -rd-max-set-size argument\n";
                abort();
            }
        } else if (strcmp(argv[i], "-rd-strong-update-unknown") == 0) {
            rd_strong_update_unknown = true;
        } else if (strcmp(argv[i], "-dot") == 0) {
            todot = true;
        } else if (strcmp(argv[i], "-threads") == 0) {
            threads = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-entry") == 0) {
            entryFunc = argv[i+1];
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

    LLVMPointerAnalysisOptions ptaopts;
    ptaopts.setEntryFunction(entryFunc);
    ptaopts.setFieldSensitivity(field_sensitivity);
    ptaopts.threads = threads;

    if (type == FLOW_INSENSITIVE) {
        ptaopts.analysisType = LLVMPointerAnalysisOptions::AnalysisType::fi;
    } else {
        ptaopts.analysisType = LLVMPointerAnalysisOptions::AnalysisType::fs;
    }

    DGLLVMPointerAnalysis PTA(M, ptaopts);

    tm.start();
    PTA.run();

    tm.stop();
    tm.report("INFO: Pointer analysis took");

    LLVMDataDependenceAnalysisOptions opts;
    opts.threads = threads;
    opts.entryFunction = entryFunc;
    opts.strongUpdateUnknown = rd_strong_update_unknown;
    opts.maxSetSize = max_set_size;
    if (rda == RdaType::SSA) {
        opts.analysisType = DataDependenceAnalysisOptions::AnalysisType::ssa;
    } else {
        opts.analysisType = DataDependenceAnalysisOptions::AnalysisType::rd;
    }

    LLVMDataDependenceAnalysis DDA(M, &PTA, opts);
    tm.start();
    DDA.run();
    tm.stop();
    tm.report("INFO: Data dependence analysis took");

    dumpDefs(&DDA, todot);

    return 0;
}
