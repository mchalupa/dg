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

#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"
#include "dg/PointerAnalysis/Pointer.h"

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/DataDependence/DataDependence.h"

#include "dg/util/debug.h"
#include "TimeMeasure.h"

using namespace dg;
using namespace dg::dda;
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
        ELEM(RWNodeType::LOAD)
        ELEM(RWNodeType::PHI)
        ELEM(RWNodeType::MU)
        ELEM(RWNodeType::CALL)
        ELEM(RWNodeType::CALL_RETURN)
        ELEM(RWNodeType::FORK)
        ELEM(RWNodeType::JOIN)
        ELEM(RWNodeType::RETURN)
        ELEM(RWNodeType::NOOP)
        ELEM(RWNodeType::NONE)
        default:
            printf("!unknown RWNodeType!");
    };
#undef ELEM
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

class Dumper {
protected:
    LLVMDataDependenceAnalysis *DDA;
    bool dot{false};

    virtual void dumpBBlockDefinitions(RWBBlock *) {}

    void printName(RWNode *node) {
        if (node == nullptr) {
            printf("nullptr");
            return;
        }

        if (node == UNKNOWN_MEMORY) {
            printf("UNKNOWN MEMORY");
            return;
        }

        const char *name = nullptr;

        std::string nm;
        if (!name) {
            if (!node->getUserData<llvm::Value>()) {
                printRWNodeType(node->getType());
                printId(node);
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

public:
    Dumper(LLVMDataDependenceAnalysis *DDA, bool todot = true)
    : DDA(DDA), dot(todot) {}

    void dump() {
        assert(dot && "Non-dot dump unsupported right now");

        printf("digraph \"Data Dependencies Graph\" {\n");
        printf("  compund=true;\n\n");

        for (auto *subg : DDA->getGraph()->subgraphs()) {
            printf("subgraph cluster_subg_%p {\n", subg);
            printf("  compund=true;\n\n");

            for (auto *block : subg->bblocks()) {
                printf("subgraph cluster_bb_%p {\n", block);

                printf("label=\"\\nblock: %p\\n", block);
                dumpBBlockDefinitions(block);
                printf("\"\nlabelloc=b\n");

                /* dump nodes */
                for(RWNode *node : block->getNodes()) {
                    nodeToDot(node);
                }

                // dump CFG edges between nodes in one block
                RWNode *last = nullptr;
                for(RWNode *node : block->getNodes()) {
                    if (last) { // successor edge
                        printf("\tNODE%p->NODE%p\n",
                               static_cast<void*>(last), static_cast<void*>(node));
                    }
                    last = node;
                }
                putchar('\n');

                // dump def-use edges
                for(RWNode *node : block->getNodes()) {
                    if (node->getType() == RWNodeType::PHI) {
                        for (RWNode *def : node->defuse) {
                            printf("\tNODE%p->NODE%p [style=dotted]",
                                   static_cast<void*>(def), static_cast<void*>(node));
                        }
                    }
                    if (node->isUse()) {
                        for (RWNode *def : DDA->getDefinitions(node)) {
                            printf("\tNODE%p->NODE%p [style=dotted color=blue]",
                                   static_cast<void*>(def), static_cast<void*>(node));
                        }
                    }
                }

                printf("}\n");
            }
            printf("}\n");

            /* dump block edges */
            for (auto bblock : subg->bblocks()) {
                for (auto *succ : bblock->getSuccessors())
                    printf("\tNODE%p -> NODE%p "
                           "[penwidth=2"
                           " lhead=\"cluster_bb_%p\""
                           " ltail=\"cluster_bb_%p\"]\n",
                           static_cast<void*>(bblock->getLast()),
                           static_cast<void*>(succ->getFirst()),
                           static_cast<void*>(bblock),
                           static_cast<void*>(succ));
            }
        }

        printf("}\n");
    }


private:

    void printId(RWNode *node) {
        if (dot)
            printf(" [%u]\\n", node->getID());
        else
            printf(" [%u]\n", node->getID());
    }

    void dumpMap(RWNode *node) {
        auto& map = node->def_map;
        for (const auto& it : map) {
            for (RWNode *site : it.second) {
                printName(it.first.target);
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

                printName(site);
                if (dot)
                    printf("\\n");
                else
                    putchar('\n');
            }
        }
    }

    void _dumpDefSites(const std::set<DefSite>& defs, const char *kind) {
        printf("-------------\\n");
        for (const DefSite& def : defs) {
            printf("%s: ", kind);
            printName(def.target);
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

    void dumpDefines(RWNode *node) {
        if (!node->getDefines().empty()) {
            _dumpDefSites(node->getDefines(), "DEF");
        }
    }

    void dumpOverwrites(RWNode *node) {
        if (!node->getOverwrites().empty()) {
            _dumpDefSites(node->getOverwrites(), "DEF strong");
        }
    }

    void dumpUses(RWNode *node) {
        if (!node->getUses().empty()) {
            _dumpDefSites(node->getUses(), "USE");
        }
    }

    void nodeToDot(RWNode *node) {
        printf("\tNODE%p [label=\"%u ", static_cast<void*>(node), node->getID());
        printName(node);
        if (node->getSize() > 0) {
            printf("\\n[size: %lu]\\n", node->getSize());
            printf("\\n-------------\\n");
        }

        if (verbose) {
            printf("\\nblock: %p\\n", node->getBBlock());
            printf("\\n-------------\\n");

            dumpDefines(node);
            dumpOverwrites(node);
            dumpUses(node);
            printf("\\n-------------\\n");
        }

        dumpMap(node);

        printf("\" shape=box]\n");
    }

    /*
    void dumpRWNode(RWNode *n) {
        printf("NODE: ");
        if (n == nullptr) {
            printf("nullptr\n");
            return;
        }
        printName(n, false);
        if (n->getSize() > 0)
            printf(" [size: %lu]", n->getSize());
        putchar('\n');
        dumpMap(n);
        printf("---\n");
    }
    */
};

class MemorySSADumper : public Dumper {

    void dumpDDIMap(const DefinitionsMap<RWNode>& map) {
        for (const auto& it : map) {
           printf("\\l----  ");
           printName(it.first);
           printf("  ----\\l");
           for (auto& it2 : it.second) {
               printInterval(it2.first, "  ", " => \\l");
               for (auto where : it2.second) {
                   printf("      ");
                   printName(where);
                   if (dot)
                       printf("\\n");
                   else
                       putchar('\n');
               }
           }
        }
    }

public:
    MemorySSADumper(LLVMDataDependenceAnalysis *DDA, bool todot)
    : Dumper(DDA, todot) {}

    void dumpBBlockDefinitions(RWBBlock *block) override {
        auto SSA = static_cast<MemorySSATransformation*>(DDA->getDDA()->getImpl());
        auto *D = SSA->getBBlockDefinitions(block);
        if (!D)
            return;
        printf("\\n====  defines ====\\n");
        dumpDDIMap(D->definitions);
        printf("\\n====  kills ====\\n");
        dumpDDIMap(D->definitions);
        if (!D->allDefinitions.empty()) {
            printf("\\n==== all defs cache ====\\n");
            dumpDDIMap(D->allDefinitions);
        }
    }
};

static void
dumpDefs(LLVMDataDependenceAnalysis *DDA, bool todot)
{
    assert(DDA);

    if (DDA->getOptions().isSSA()) {
        MemorySSADumper dumper(DDA, todot);
        dumper.dump();
    } else {
        Dumper dumper(DDA, todot);
        dumper.dump();
    }
}

int main(int argc, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    bool todot = false;
    bool threads = false;
    bool graph_only = false;
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
    } rda = RdaType::SSA;

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
        } else if (strcmp(argv[i], "-dbg") == 0) {
            DBG_ENABLE();
        } else if (strcmp(argv[i], "-graph-only") == 0) {
            graph_only = true;
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

    tm.start();
    LLVMDataDependenceAnalysis DDA(M, &PTA, opts);
    if (graph_only) {
        DDA.buildGraph();
    } else {
        DDA.run();
    }
    tm.stop();
    tm.report("INFO: Data dependence analysis took");

    dumpDefs(&DDA, todot);

    return 0;
}
