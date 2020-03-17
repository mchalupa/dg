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

static inline size_t count_ws(const std::string& str) {
    size_t n = 0;
    while (isspace(str[n])) {
        ++n;
    }
    return n;
}

static inline size_t trim_name_idx(const std::string& str) {
    // skip, e.g., align attributes, etc.
    auto m = str.rfind(", align");
    if (m == std::string::npos)
        return str.length();
    return m - 1;
}

static std::string
getInstName(const llvm::Value *val) {
    assert(val);
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    ro << *val;
    ro.flush();

    auto str = ostr.str();
    auto n = count_ws(str);
    auto m = trim_name_idx(str);
    if (n > 0)
        str = str.substr(n, m);

    if (auto *I = llvm::dyn_cast<llvm::Instruction>(val)) {
        const auto& fun = I->getParent()->getParent()->getName();
        auto funstr = fun.str();
        if (funstr.length() > 15)
            funstr = funstr.substr(0, 15);
        funstr += "::";
        funstr += str;
        return funstr;
    }

    return str;
}

static void printRWNodeType(enum RWNodeType type) {
#define ELEM(t) case(t): do {printf("%s", #t); }while(0); break;
    switch(type) {
        ELEM(RWNodeType::ALLOC)
        ELEM(RWNodeType::DYN_ALLOC)
        ELEM(RWNodeType::STORE)
        ELEM(RWNodeType::LOAD)
        ELEM(RWNodeType::PHI)
        ELEM(RWNodeType::MU)
        ELEM(RWNodeType::CALL)
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

    void printName(const RWNode *node) {
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
            auto *val = DDA->getValue(node);
            if (!val) {
                printRWNodeType(node->getType());
                return;
            }

            nm = getInstName(val);
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
        printf("  compound=true;\n\n");

        for (auto *subg : DDA->getGraph()->subgraphs()) {
            printf("subgraph cluster_subg_%p {\n", subg);
            printf("  compound=true;\n\n");

            printf("  label=\"subgraph:\\n\";\n");
            printf("  style=filled;\n");
            printf("  color=white;\n");

            for (auto *block : subg->bblocks()) {
                printf("subgraph cluster_bb_%p {\n", block);
                printf("    style=filled;\n");
                printf("    fillcolor=\"#eeeeee\";\n");
                printf("    color=\"black\";\n");

                puts("label=<<table border=\"0\">");
                printf("<tr><td colspan=\"4\">bblock: %p</td></tr>", block);
                dumpBBlockDefinitions(block);
                printf("</table>>\nlabelloc=b\n");

                /* dump nodes */
                if (block->empty()) {
                    // if the block is empty, create at least a
                    // dummy node so that we can draw CFG edges to it
                    printf("\tNODE%p [label=\"empty blk\"]\n",
                           static_cast<void*>(block));
                } else {
                    for(RWNode *node : block->getNodes()) {
                        nodeToDot(node);
                    }
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

                // dump def-use edges and call edges
                for(RWNode *node : block->getNodes()) {
                    if (node->getType() == RWNodeType::PHI) {
                        for (RWNode *def : node->defuse) {
                            printf("\tNODE%p->NODE%p [style=dotted]\n",
                                   static_cast<void*>(def), static_cast<void*>(node));
                        }
                    }
                    if (node->isUse()) {
                        for (RWNode *def : DDA->getDefinitions(node)) {
                            printf("\tNODE%p->NODE%p [style=dotted color=blue]\n",
                                   static_cast<void*>(def), static_cast<void*>(node));
                        }
                    }
                    if (auto *C = RWNodeCall::get(node)) {
                        for (auto& cv : C->getCallees()) {
                            if (auto *s = cv.getSubgraph()) {
                                assert(s->getRoot() && "Subgraph has no root");
                                printf("\tNODE%p->NODE%p "
                                       "[penwidth=4 color=blue "
                                       "ltail=cluster_subg_%p]\n",
                                       static_cast<void*>(C),
                                       static_cast<const void*>(s->getRoot()), s);
                            }
                        }
                    }
                }

                printf("}\n");
            }
            printf("}\n");

            /* dump block edges */
            for (auto bblock : subg->bblocks()) {
                for (auto *succ : bblock->getSuccessors()) {
                    printf("\tNODE%p -> NODE%p "
                           "[penwidth=2"
                           " lhead=\"cluster_bb_%p\""
                           " ltail=\"cluster_bb_%p\"]\n",
                           bblock->empty() ? static_cast<void*>(bblock) :
                                             static_cast<void*>(bblock->getLast()),
                           succ->empty() ? static_cast<void*>(succ) :
                                           static_cast<void*>(succ->getFirst()),
                           static_cast<void*>(bblock),
                           static_cast<void*>(succ));
                }
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
        if (map.empty()) {
            return;
        }

        printf("----- def_map -----\\n\n");
        for (const auto& it : map) {
            for (RWNode *site : it.second) {
                printName(it.first.target);
                // don't print offsets with unknown memory
                if (it.first.target == UNKNOWN_MEMORY) {
                    printf(" => ");
                } else {
                    if (it.first.offset.isUnknown())
                        printf(" | ? | => ");
                    else if (it.first.len.isUnknown())
                        printf(" | %lu - ? | => ", *it.first.offset);
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

    void _dumpDefSites(const std::set<DefSite>& defs,
                       const char *kind) {
        if (defs.empty())
            return;

        printf("<tr><td></td><td>------ %s ------</td></tr>\n", kind);
        for (const DefSite& def : defs) {
            puts("<tr><td></td><td>");
            printName(def.target);
                if (def.offset.isUnknown())
                    printf(" [? - ");
                else
                    printf(" [%lu - ", *def.offset);

                if (def.len.isUnknown())
                    printf("?]");
                else
                    printf("%lu]", *def.offset + (*def.len - 1));
            puts("</td></tr>\n");
        }
    }

    void dumpDefines(const RWNode *node) {
        if (!node->getDefines().empty()) {
            _dumpDefSites(node->getDefines(), "defines");
        }
    }

    void dumpOverwrites(const RWNode *node) {
        if (!node->getOverwrites().empty()) {
            _dumpDefSites(node->getOverwrites(), "overwrites");
        }
    }

    void dumpUses(const RWNode *node) {
        if (!node->getUses().empty()) {
            _dumpDefSites(node->getUses(), "uses");
        }
    }

    void nodeToDot(const RWNode *node) {
        printf("\tNODE%p ", static_cast<const void*>(node));
        printf("[label=<<table border=\"0\"><tr><td>(%u)</td> ", node->getID());
        printf("<td><font color=\"#af0000\">");
        printName(node);
        printf("</font></td>");
        printf("</tr>\n");

        if (node->getSize() > 0) {
              printf("<tr><td></td><td>size: %lu</td></tr>\n", node->getSize());
        }

        if (verbose) {
            dumpDefines(node);
            dumpOverwrites(node);
            dumpUses(node);
        }

        // dumped data for undefined functions
        // (call edges will be dumped with other edges)
        if (auto *C = RWNodeCall::get(node)) {
            for (auto& cv : C->getCallees()) {
                if (const RWNode *undef = cv.getCalledValue()) {
                    printf("<tr><td></td><td>------ undef call ------</td></tr>\n");
                    dumpDefines(undef);
                    dumpOverwrites(undef);
                    dumpUses(undef);
                }
            }
        }

      ////dumpMap(node);

        puts("</table>>"); // end of label
        printf(" style=filled fillcolor=white shape=box]\n");
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
           for (auto& it2 : it.second) {
                printf("<tr><td align=\"left\" colspan=\"4\">");
                printName(it.first);
                printf("</td></tr>");
               for (auto where : it2.second) {
                printf("<tr><td>&nbsp;&nbsp;</td><td>");
                printInterval(it2.first);
                printf("</td><td>@</td><td>");
                   printName(where);
                puts("</td></tr>");
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
        printf("<tr><td colspan=\"4\">==  defines ==</td></tr>");
        dumpDDIMap(D->definitions);
        printf("<tr><td colspan=\"4\">==  kills ==</td></tr>");
        dumpDDIMap(D->definitions);
        if (!D->allDefinitions.empty()) {
            printf("<tr><td colspan=\"4\">== all defs cached ==</td></tr>");
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
