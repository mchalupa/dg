#include <set>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cassert>
#include <cinttypes>
#include <cstdio>

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
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
SILENCE_LLVM_WARNINGS_POP

#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"
#include "dg/PointerAnalysis/Pointer.h"

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/DataDependence/DataDependence.h"

#include "dg/util/debug.h"
#include "dg/tools/TimeMeasure.h"
#include "dg/tools/llvm-slicer-utils.h"
#include "dg/tools/llvm-slicer-opts.h"

using namespace dg;
using namespace dg::dda;
using llvm::errs;

llvm::cl::opt<bool> enable_debug("dbg",
    llvm::cl::desc("Enable debugging messages (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> verbose("v",
    llvm::cl::desc("Verbose output (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> graph_only("graph-only",
    llvm::cl::desc("Dump only graph, do not run any analysis (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> todot("dot",
    llvm::cl::desc("Output in graphviz format (forced atm.)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> quiet("q",
    llvm::cl::desc("No output (for benchmarking)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dump_c_lines("c-lines",
    llvm::cl::desc("Dump output as C lines (line:column where possible)."
                   "Requires metadata in the bitcode (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

using VariablesMapTy = std::map<const llvm::Value *, CVariableDecl>;
VariablesMapTy allocasToVars(const llvm::Module& M);
VariablesMapTy valuesToVars;

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


    if (dump_c_lines) {
        if (auto *I = llvm::dyn_cast<llvm::Instruction>(val)) {
            auto& DL = I->getDebugLoc();
            if (DL) {
                ro << DL.getLine() << ":" << DL.getCol();
            } else {
                auto Vit = valuesToVars.find(I);
                if (Vit != valuesToVars.end()) {
                    auto& decl = Vit->second;
                    ro << decl.line << ":" << decl.col;
                } else {
                    ro << "(no dbg) ";
                    ro << *val;
                }
            }
        }
    } else {
        ro << *val;
    }

    ro.flush();

    auto str = ostr.str();
    auto n = count_ws(str);
    auto m = trim_name_idx(str);
    if (n > 0)
        str = str.substr(n, m);

    if (dump_c_lines)
        return str;

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
        ELEM(RWNodeType::GLOBAL)
        ELEM(RWNodeType::STORE)
        ELEM(RWNodeType::LOAD)
        ELEM(RWNodeType::PHI)
        ELEM(RWNodeType::INARG)
        ELEM(RWNodeType::OUTARG)
        ELEM(RWNodeType::CALLIN)
        ELEM(RWNodeType::CALLOUT)
        ELEM(RWNodeType::MU)
        ELEM(RWNodeType::CALL)
        ELEM(RWNodeType::FORK)
        ELEM(RWNodeType::JOIN)
        ELEM(RWNodeType::RETURN)
        ELEM(RWNodeType::NOOP)
        ELEM(RWNodeType::GENERIC)
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
        printf("[%" PRIu64 " - ", *I.start);

    if (I.end.isUnknown())
        printf("?]");
    else
        printf("%" PRIu64 "]", *I.end);

    if (suff)
        printf("%s", suff);
}

class Dumper {
protected:
    LLVMDataDependenceAnalysis *DDA;
    bool dot{false};

    virtual void dumpBBlockDefinitions(RWBBlock *) {}

    virtual void dumpSubgraphLabel(RWSubgraph *subgraph) {
        printf("  label=\"subgraph: %s(%p)\\n\";\n",
               subgraph->getName().c_str(), subgraph);
    }

    void printName(const RWNode *node) {
        if (node == nullptr) {
            printf("nullptr");
            return;
        }

        if (node == UNKNOWN_MEMORY) {
            printf("unknown mem");
            return;
        }

        const char *name = nullptr;

        std::string nm;
        if (!name) {
            auto *val = DDA->getValue(node);
            if (!val) {
                printRWNodeType(node->getType());
                printId(node);
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

    void nodeToDot(RWNode *node) {
        static std::set<RWNode *> _dumped;
        if (!_dumped.insert(node).second) // already dumped
            return;

        printf("\tNODE%p ", static_cast<const void*>(node));
        printf("[label=<<table border=\"0\"><tr><td>(%u)</td> ", node->getID());
        printf("<td><font color=\"#af0000\">");
        printName(node);
        printf("</font></td>");
        printf("</tr>\n");

        if (node->getSize() > 0) {
              printf("<tr><td></td><td>size: %zu</td></tr>\n", node->getSize());
        }

        if (verbose) {
            printf("<tr><td>type:</td><td>");
            printRWNodeType(node->getType());
            printf("</td></tr>\n");
            printf("<tr><td colspan=\"2\">bblock: %p</td></tr>\n", node->getBBlock());
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

        puts("</table>>"); // end of label
        printf(" style=filled fillcolor=white shape=box]\n");
    }

    void dumpNodeEdges(RWNode *node) {
        static std::set<RWNode *> dumped;
        if (!dumped.insert(node).second)
            return;

        if (verbose || node->isPhi()) {
            for (RWNode *def : node->defuse) {
                printf("\tNODE%p->NODE%p [style=dotted constraint=false]\n",
                       static_cast<void*>(def), static_cast<void*>(node));
            }
        }
        if (!graph_only && node->isUse()) {
            for (RWNode *def : DDA->getDefinitions(node)) {
                nodeToDot(def);
                printf("\tNODE%p->NODE%p [style=dotted constraint=false color=blue]\n",
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
                } else {
                    printf("\tNODE%p->NODE%p [style=dashed constraint=false color=blue]\n",
                           static_cast<void*>(C), static_cast<void*>(cv.getCalledValue()));
                }
            }
        }
    }

public:
    Dumper(LLVMDataDependenceAnalysis *DDA, bool todot = false)
    : DDA(DDA), dot(todot) {}

    void dumpBBlockEdges(RWBBlock *block) {
        // dump CFG edges between nodes in one block
        RWNode *last = nullptr;
        for(RWNode *node : block->getNodes()) {
            if (last) { // successor edge
                printf("\tNODE%p->NODE%p [constraint=true]\n",
                       static_cast<void*>(last), static_cast<void*>(node));
            }
            last = node;
        }
        putchar('\n');
    }

    void dumpBBlock(RWBBlock *block) {
        printf("subgraph cluster_bb_%p {\n", block);
        printf("    style=filled;\n");
        printf("    fillcolor=\"#eeeeee\";\n");
        printf("    color=\"black\";\n");

        puts("label=<<table border=\"0\">");
        printf("<tr><td colspan=\"4\">bblock %u (%p)</td></tr>",
               block->getID(), block);
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

                if (auto *C = RWNodeCall::get(node)) {
                    for (auto& cv : C->getCallees()) {
                        if (auto *val = cv.getCalledValue())
                            nodeToDot(val);
                    }

                    for (auto *i : C->getInputs()) {
                        nodeToDot(i);
                    }
                    for (auto *o : C->getOutputs()) {
                        nodeToDot(o);
                    }
                }
            }
        }

        printf("}\n");
    }


    void dump() {
        if (dot)
            dumpToDot();
        else
            dumpToTty();
    }

    void dumpRWNode(RWNode *n) {
        printf("NODE [%u]: ", n->getID());
        if (n == nullptr) {
            printf("nullptr\n");
            return;
        }
        printName(n);
        if (n->getSize() > 0)
            printf(" [size: %zu]", n->getSize());
        putchar('\n');
    }

    void dumpToTty() {

        for (auto *subg : DDA->getGraph()->subgraphs()) {
            printf("=========== fun: %s ===========\n", subg->getName().c_str());
            for (auto *bb : subg->bblocks()) {
                printf("<<< bblock: %u >>>\n", bb->getID());
                for (auto *node : bb->getNodes()) {
                    dumpRWNode(node);
                    if (!graph_only && node->isUse() && !node->isPhi()) {
                        for (RWNode *def : DDA->getDefinitions(node)) {
                            printf("  <- ");
                            printName(def);
                            putchar('\n');
                        }
                    }
                }
            }
        }
    }

    void dumpToDot() {
        assert(dot && "Non-dot dump unsupported right now");

        printf("digraph \"Data Dependencies Graph\" {\n");
        printf("  compound=true;\n\n");


        /*
        for (auto *global : DDA->getGraph()->getGlobals()) {
            nodeToDot(global);
        }
        */

        for (auto *subg : DDA->getGraph()->subgraphs()) {
            printf("subgraph cluster_subg_%p {\n", subg);
            printf("  compound=true;\n\n");
            printf("  style=filled;\n");
            printf("  fillcolor=white; color=blue;\n");

            dumpSubgraphLabel(subg);

            // dump summary nodes
            auto SSA = static_cast<MemorySSATransformation*>(DDA->getDDA()->getImpl());
            const auto *summary = SSA->getSummary(subg);
            if (summary) {
                for (auto& i : summary->inputs) {
                    for (auto& it : i.second)
                        for (auto *nd : it.second)
                            nodeToDot(nd);
                }
                for (auto& o : summary->outputs) {
                    for (auto& it : o.second)
                        for (auto *nd : it.second)
                            nodeToDot(nd);
                }
            }

            for (auto *block : subg->bblocks()) {
                dumpBBlock(block);
            }
            printf("}\n");
        }

        for (auto *subg : DDA->getGraph()->subgraphs()) {
            // dump summary nodes edges
            auto SSA = static_cast<MemorySSATransformation*>(DDA->getDDA()->getImpl());
            const auto *summary = SSA->getSummary(subg);
            if (summary) {
                for (auto& i : summary->inputs) {
                    for (auto& it : i.second)
                        for (auto *nd : it.second)
                            dumpNodeEdges(nd);
                }
                for (auto& o : summary->outputs) {
                    for (auto& it : o.second)
                        for (auto *nd : it.second)
                            dumpNodeEdges(nd);
                }
            }

            // CFG
            for (auto bblock : subg->bblocks()) {
                dumpBBlockEdges(bblock);

                for (auto *succ : bblock->successors()) {
                    printf("\tNODE%p -> NODE%p "
                           "[penwidth=2 constraint=true"
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

            // def-use
            for (auto bblock : subg->bblocks()) {
                for (auto *node : bblock->getNodes()) {
                    dumpNodeEdges(node);

                    if (auto *C = RWNodeCall::get(node)) {
                        for (auto *n : C->getInputs())
                            dumpNodeEdges(n);
                        for (auto *n : C->getOutputs())
                            dumpNodeEdges(n);
                    }
                }
            }
        }

        printf("}\n");
    }


private:

    void printId(const RWNode *node) {
        printf(" [%u]", node->getID());
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
                    printf(" [%" PRIu64 " - ", *def.offset);

                if (def.len.isUnknown())
                    printf("?]");
                else
                    printf("%" PRIu64 "]", *def.offset + (*def.len - 1));
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
};

class MemorySSADumper : public Dumper {

    void _dumpDefSites(RWNode *n, const std::set<DefSite>& defs) {
        if (defs.empty())
            return;

        for (const DefSite& def : defs) {
            printf("<tr><td>at (%u): </td><td>(%u)</td><td>",
                   n->getID(), def.target->getID());
            printName(def.target);
            printf("</td><td>");
                if (def.offset.isUnknown())
                    printf(" [? - ");
                else
                    printf(" [%" PRIu64 " - ", *def.offset);

                if (def.len.isUnknown())
                    printf("?]");
                else
                    printf("%" PRIu64 "]", *def.offset + (*def.len - 1));
            puts("</td></tr>\n");
        }
    }

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

    void dumpBBlockDefinitions(RWBBlock *block) override {
        auto SSA = static_cast<MemorySSATransformation*>(DDA->getDDA()->getImpl());
        auto *D = SSA->getDefinitions(block);
        if (!D)
            return;
        printf("<tr><td colspan=\"4\">==  defines ==</td></tr>");
        dumpDDIMap(D->definitions);
        printf("<tr><td colspan=\"4\">==  kills ==</td></tr>");
        dumpDDIMap(D->kills);
    }

    void dumpSubgraphLabel(RWSubgraph *subgraph) override {
        auto SSA = static_cast<MemorySSATransformation*>(DDA->getDDA()->getImpl());
        const auto *summary = SSA->getSummary(subgraph);

        if (!summary) {
            printf("  label=<<table cellborder=\"0\">\n"
                                   "<tr><td>subgraph %s(%p)</td></tr>\n"
                                   "<tr><td>no summary</td></tr></table>>;\n",
                                   subgraph->getName().c_str(), subgraph);
            return;
        }

        printf("  label=<<table cellborder=\"0\"><tr><td colspan=\"4\">subgraph %s (%p)</td></tr>\n"
                               "<tr><td colspan=\"4\">-- summary -- </td></tr>\n",
                               subgraph->getName().c_str(), subgraph);
        printf("<tr><td colspan=\"4\">==  inputs ==</td></tr>");
        dumpDDIMap(summary->inputs);
        printf("<tr><td colspan=\"4\">==  outputs ==</td></tr>");
        dumpDDIMap(summary->outputs);
        printf("</table>>;\n");
    }


public:
    MemorySSADumper(LLVMDataDependenceAnalysis *DDA, bool todot)
    : Dumper(DDA, todot) {}

};

static void
dumpDefs(LLVMDataDependenceAnalysis *DDA, bool todot)
{
    assert(DDA);

    if (DDA->getOptions().isSSA()) {
        auto SSA = static_cast<MemorySSATransformation*>(DDA->getDDA()->getImpl());
        if (!graph_only)
            SSA->computeAllDefinitions();

        if (quiet)
            return;

        MemorySSADumper dumper(DDA, todot);
        dumper.dump();
    } else {
        Dumper dumper(DDA, todot);
        dumper.dump();
    }
}

std::unique_ptr<llvm::Module> parseModule(llvm::LLVMContext& context,
                                          const SlicerOptions& options)
{
    llvm::SMDiagnostic SMD;

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 5))
    auto _M = llvm::ParseIRFile(options.inputFile, SMD, context);
    auto M = std::unique_ptr<llvm::Module>(_M);
#else
    auto M = llvm::parseIRFile(options.inputFile, SMD, context);
    // _M is unique pointer, we need to get Module *
#endif

    if (!M) {
        SMD.print("llvm-dda-dump", llvm::errs());
    }

    return M;
}

int main(int argc, char *argv[])
{
    SlicerOptions options = parseSlicerOptions(argc, argv);

    if (enable_debug) {
        DBG_ENABLE();
    }

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> M = parseModule(context, options);
    if (!M) {
        llvm::errs() << "Failed parsing '" << options.inputFile << "' file:\n";
        return 1;
    }

    if (!M->getFunction(options.dgOptions.entryFunction)) {
        llvm::errs() << "The entry function not found: "
                     << options.dgOptions.entryFunction << "\n";
        return 1;
    }

    debug::TimeMeasure tm;

    DGLLVMPointerAnalysis PTA(M.get(), options.dgOptions.PTAOptions);

    tm.start();
    PTA.run();

    tm.stop();
    tm.report("INFO: Pointer analysis took");

    tm.start();
    LLVMDataDependenceAnalysis DDA(M.get(), &PTA, options.dgOptions.DDAOptions);
    if (graph_only) {
        DDA.buildGraph();
    } else {
        DDA.run();
    }
    tm.stop();
    tm.report("INFO: Data dependence analysis took");

    if (dump_c_lines) {
#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
        llvm::errs() << "WARNING: Variables names matching is not supported for LLVM older than 3.7\n";
#else
        valuesToVars = allocasToVars(*M);
#endif // LLVM > 3.6
        if (valuesToVars.empty()) {
            llvm::errs() << "WARNING: No debugging information found, "
                         << "the C lines output will be corrupted\n";
        }
    }

    dumpDefs(&DDA, todot);

    return 0;
}
