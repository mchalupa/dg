#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <set>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif
SILENCE_LLVM_WARNINGS_POP

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/PointerAnalysis/Pointer.h"

#include "dg/tools/llvm-slicer-utils.h"
#include "dg/tools/llvm-slicer-opts.h"
#include "dg/tools/TimeMeasure.h"

using namespace dg;
using namespace dg::pta;
using dg::debug::TimeMeasure;
using llvm::errs;

using PTType = dg::LLVMPointerAnalysisOptions::AnalysisType;

llvm::cl::opt<bool> enable_debug("dbg",
    llvm::cl::desc("Enable debugging messages (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> verbose("v",
    llvm::cl::desc("Enable verbose output (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> verbose_more("vv",
    llvm::cl::desc("Enable verbose output (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> ids_only("ids-only",
    llvm::cl::desc("Dump only IDs of nodes, not instructions (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dump_graph_only("graph-only",
    llvm::cl::desc("Dump only graph (do not run the analysis) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> names_with_funs("names-with-funs",
    llvm::cl::desc("Dump names of functions with instructions (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> callgraph("callgraph",
    llvm::cl::desc("Dump also call graph (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> callgraph_only("callgraph-only",
    llvm::cl::desc("Dump only call graph (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<uint64_t> dump_iteration("iteration",
    llvm::cl::desc("Stop and dump analysis after the given iteration."),
    llvm::cl::init(0), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<std::string> display_only("display-only",
    llvm::cl::desc("Show results only for the given function(s) (separated by comma)."),
    llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> _stats("statistics",
    llvm::cl::desc("Dump statistics (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> _quiet("q",
    llvm::cl::desc("Quite mode - no output (for benchmarking) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> todot("dot",
    llvm::cl::desc("Dump IR to graphviz format (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dump_ir("ir",
    llvm::cl::desc("Dump IR of the analysis (DG analyses only) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dump_c_lines("c-lines",
    llvm::cl::desc("Dump output as C lines (line:column where possible)."
                   "Requires metadata in the bitcode (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

using VariablesMapTy = std::map<const llvm::Value *, CVariableDecl>;
VariablesMapTy allocasToVars(const llvm::Module& M);
VariablesMapTy valuesToVars;

static std::vector<const llvm::Function *> display_only_func;

std::unique_ptr<LLVMPointerAnalysis> PA;

static std::string valToStr(const llvm::Value *val) {
    using namespace llvm;

    std::ostringstream ostr;
    raw_os_ostream ro(ostr);

    if (auto *F = dyn_cast<Function>(val)) {
        ro << "fun '" << F->getName().str() << "'";
    } else {
        auto *I = dyn_cast<Instruction>(val);
        if (dump_c_lines) {
            if (I) {
                auto& DL = I->getDebugLoc();
                if (DL) {
                    ro << DL.getLine() << ":" << DL.getCol();
                } else {
                    auto Vit = valuesToVars.find(I);
                    if (Vit != valuesToVars.end()) {
                        auto& decl = Vit->second;
                        ro << decl.line << ":" << decl.col;
                    }
                }
            }
        } else {
            if (I) {
                ro << I->getParent()->getParent()->getName().str();
                ro << "::";
            }

            assert(val);
            ro << *val;
        }
    }

    ro.flush();

    return ostr.str();
}

// FIXME: get rid of this...
static std::string getInstName(const llvm::Value *val) {
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    if (names_with_funs) {
        if (auto I = llvm::dyn_cast<llvm::Instruction>(val)) {
            ro << I->getParent()->getParent()->getName().data() << ":";
        }
    }

    assert(val);
    if (llvm::isa<llvm::Function>(val))
        ro << val->getName().data();
    else
        ro << *val;

    ro.flush();

    // break the string if it is too long
    return ostr.str();
}

void printPSNodeType(enum PSNodeType type) {
    printf("%s", PSNodeTypeToCString(type));
}

static void dumpPointer(const Pointer& ptr, bool dot);

static void printName(PSNode *node, bool dot = false) {
    std::string nm;
    const char *name = nullptr;
    if (node->isNull()) {
        name = "null";
    } else if (node->isUnknownMemory()) {
        name = "unknown";
    } else if (node->isInvalidated() &&
        !node->getUserData<llvm::Value>()) {
            name = "invalidated";
    }

    if (!name) {
        if (ids_only) {
            printf(" <%u>", node->getID());
            return;
        }

        if (!node->getUserData<llvm::Value>()) {
            if (dot) {
                printf("<%u> (no name)\\n", node->getID());

                if (node->getType() == PSNodeType::CONSTANT) {
                    dumpPointer(*(node->pointsTo.begin()), dot);
                } else if (node->getType() == PSNodeType::CALL_RETURN) {
                    if (PSNode *paired = node->getPairedNode())
                        printName(paired, dot);
                } else if (PSNodeEntry *entry = PSNodeEntry::get(node)) {
                    printf("%s\\n", entry->getFunctionName().c_str());
                }
            } else {
                printf("<%u> ", node->getID());
                printPSNodeType(node->getType());
            }

            return;
        }

        nm = getInstName(node->getUserData<llvm::Value>());
        name = nm.c_str();
    }

    if (ids_only) {
        printf(" <%u>", node->getID());
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

static void dumpPointer(const Pointer& ptr, bool dot) {
    printName(ptr.target, dot);

    if (ptr.offset.isUnknown())
        printf(" + UNKNOWN");
    else
        printf(" + %" PRIu64, *ptr.offset);
}

static void dumpMemoryObject(MemoryObject *mo, int ind, bool dot) {
    bool printed_multi = false;
    for (auto& it : mo->pointsTo) {
        int width = 0;
        for (const Pointer& ptr : it.second) {
            // print indentation
            printf("%*s", ind, "");

            if (width > 0) {
                    printf("%*s -> ", width, "");
            } else {
                if (it.first.isUnknown())
                    width = printf("[??]");
                else
                    width = printf("[%" PRIu64 "]", *it.first);

                // print a new line if there are multiple items
                if (dot &&
                    (it.second.size() > 1 ||
                     (printed_multi && mo->pointsTo.size() > 1))) {
                    printed_multi = true;
                    printf("\\l%*s",ind + width, "");
                }

                printf(" -> ");

                assert(width > 0);
            }

            dumpPointer(ptr, dot);

            if (dot)
                printf("\\l");
            else
                putchar('\n');
        }
    }
}

static void
dumpMemoryMap(PointerAnalysisFS::MemoryMapT *mm, int ind, bool dot) {
    for (const auto& it : *mm) {
        // print the key
        if (!dot)
            printf("%*s", ind, "");

        putchar('<');
        printName(it.first, dot);
        putchar('>');

        if (dot)
            printf("\\l");
        else
            putchar('\n');

        dumpMemoryObject(it.second.get(), ind + 4, dot);
    }
}

static bool mmChanged(PSNode *n) {
    if (n->predecessorsNum() == 0)
        return true;

    PointerAnalysisFS::MemoryMapT *mm
        = n->getData<PointerAnalysisFS::MemoryMapT>();

    for (PSNode *pred : n->predecessors()) {
        if (pred->getData<PointerAnalysisFS::MemoryMapT>() != mm)
            return true;
    }

    return false;
}

static void
dumpPointerGraphData(PSNode *n, PTType type, bool dot = false) {
    assert(n && "No node given");
    if (type == dg::LLVMPointerAnalysisOptions::AnalysisType::fi) {
        MemoryObject *mo = n->getData<MemoryObject>();
        if (!mo)
            return;

        if (dot)
            printf("\\n    Memory: ---\\n");
        else
            printf("    Memory: ---\n");

        dumpMemoryObject(mo, 6, dot);

        if (!dot)
            printf("    -----------\n");
    } else {
        PointerAnalysisFS::MemoryMapT *mm
            = n->getData<PointerAnalysisFS::MemoryMapT>();
        if (!mm)
            return;

        if (dot)
            printf("\\n------\\n    --- Memory map [%p] ---\\n", static_cast<void*>(mm));
        else
            printf("    Memory map: [%p]\n", static_cast<void*>(mm));

        if (verbose_more || mmChanged(n))
            dumpMemoryMap(mm, 6, dot);

        if (!dot)
            printf("    ----------------\n");
    }
}

static void
dumpPSNode(PSNode *n, PTType type) {
    printf("NODE %3u: ", n->getID());
    printName(n);

    PSNodeAlloc *alloc = PSNodeAlloc::get(n);
    if (alloc &&
        (alloc->getSize() || alloc->isHeap() || alloc->isZeroInitialized()))
        printf(" [size: %zu, heap: %u, zeroed: %u]",
               alloc->getSize(), alloc->isHeap(), alloc->isZeroInitialized());

    printf(" (points-to size: %zu)\n", n->pointsTo.size());

    for (const Pointer& ptr : n->pointsTo) {
        printf("    -> ");
        printName(ptr.target, false);
        if (ptr.offset.isUnknown())
            puts(" + Offset::UNKNOWN");
        else
            printf(" + %" PRIu64 "\n", *ptr.offset);
    }
    if (verbose) {
        dumpPointerGraphData(n, type);
    }
}

static void
dumpNodeToDot(PSNode *node, PTType type) {
    printf("\tNODE%u [label=\"<%u> ", node->getID(), node->getID());
    printPSNodeType(node->getType());
    printf("\\n");
    printName(node, true);
    printf("\\nparent: %u\\n", node->getParent() ? node->getParent()->getID() : 0);

    PSNodeAlloc *alloc = PSNodeAlloc::get(node);
    if (alloc && (alloc->getSize() || alloc->isHeap() || alloc->isZeroInitialized()))
        printf("\\n[size: %zu, heap: %u, zeroed: %u]",
           alloc->getSize(), alloc->isHeap(), alloc->isZeroInitialized());
    if (verbose) {
       if (PSNodeEntry *entry = PSNodeEntry::get(node)) {
           printf("called from: [");
           for (auto r : entry->getCallers())
               printf("%u ", r->getID());
           printf("]\\n");
       }
       if (PSNodeCallRet *CR = PSNodeCallRet::get(node)) {
           printf("returns from: [");
           for (auto r : CR->getReturns())
               printf("%u ", r->getID());
           printf("]\\n");
       }
       if (PSNodeRet *R = PSNodeRet::get(node)) {
           printf("returns to: [");
           for (auto r : R->getReturnSites())
               printf("%u ", r->getID());
           printf("]\\n");
       }
     }

    if (verbose && node->getOperandsNum() > 0) {
        printf("\\n--- operands ---\\n");
        for (PSNode *op : node->getOperands()) {
            printName(op, true);
        }
        printf("\\n------\\n");
    }

    if (verbose && !node->pointsTo.empty()) {
        printf("\\n--- points-to set ---\\n");
    }

    for (const Pointer& ptr : node->pointsTo) {
        printf("\\n    -> ");
        printName(ptr.target, true);
        printf(" + ");
        if (ptr.offset.isUnknown())
            printf("Offset::UNKNOWN");
        else
            printf("%" PRIu64, *ptr.offset);
    }

    if (verbose) {
        dumpPointerGraphData(node, type, true /* dot */);
    }

    printf("\", shape=box");
    if (node->getType() != PSNodeType::STORE) {
        if (node->pointsTo.size() == 0
            && (node->getType() == PSNodeType::LOAD ||
                node->getType() == PSNodeType::GEP  ||
                node->getType() == PSNodeType::CAST ||
                node->getType() == PSNodeType::PHI))
            printf(", style=filled, fillcolor=red");
    } else {
        printf(", style=filled, fillcolor=orange");
    }

    printf("]\n");
}

static void dumpNodeEdgesToDot(PSNode *node) {
    for (PSNode *succ : node->successors()) {
        printf("\tNODE%u -> NODE%u [penwidth=2]\n",
               node->getID(), succ->getID());
    }

    for (PSNode *op : node->getOperands()) {
        printf("\tNODE%u -> NODE%u [color=blue,style=dotted,constraint=false]\n",
               op->getID(), node->getID());
    }

    if (auto C = PSNodeCall::get(node)) {
        for (const auto subg : C->getCallees()) {
            printf("\tNODE%u -> NODE%u [penwidth=4,style=dashed,constraint=false]\n",
                   node->getID(), subg->root->getID());
        }
    }

    if (auto R = PSNodeRet::get(node)) {
        for (const auto succ : R->getReturnSites()) {
            printf("\tNODE%u -> NODE%u [penwidth=4,style=dashed,constraint=false]\n",
                   node->getID(), succ->getID());
        }
    }
}

PSNode *getNodePtr(PSNode *ptr) { return ptr; }
PSNode *getNodePtr(const std::unique_ptr<PSNode>& ptr) { return ptr.get(); }


template <typename ContT> static void
dumpToDot(const ContT& nodes, PTType type) {
    /* dump nodes */
    for (const auto& node : nodes) {
        if (!node)
            continue;
        dumpNodeToDot(getNodePtr(node), type);
    }

    /* dump edges */
    for (const auto& node : nodes) {
        if (!node) // node id 0 is nullptr
            continue;
        dumpNodeEdgesToDot(getNodePtr(node));
    }
}

static void
dumpPointerGraphdot(DGLLVMPointerAnalysis *pta, PTType type) {

    printf("digraph \"Pointer State Subgraph\" {\n");

    if (callgraph) {
        // dump call-graph
        const auto& CG = pta->getPS()->getCallGraph();
        for (auto& it : CG) {
            printf("NODEcg%u [label=\"%s\"]\n",
                    it.second.getID(),
                    it.first->getUserData<llvm::Function>()->getName().str().c_str());
        }
        for (auto& it : CG) {
            for (auto succ : it.second.getCalls()) {
                printf("NODEcg%u -> NODEcg%u\n", it.second.getID(), succ->getID());
            }
        }
        if (callgraph_only) {
            printf("}\n");
            return;
        }
    }


    if (!display_only_func.empty()) {
        std::set<PSNode *> nodes;
        for (auto llvmFunc : display_only_func) {
            auto func_nodes = pta->getFunctionNodes(llvmFunc);
            if (func_nodes.empty()) {
                llvm::errs() << "ERROR: Did not find any nodes for function "
                             << display_only << "\n";
            } else {
                llvm::errs() << "Found " << func_nodes.size() << " nodes for function "
                             << display_only << "\n";
            }

            // use std::set to get rid of duplicates
            for (auto nd : func_nodes) {
                nodes.insert(nd);
                // get also operands of the nodes,
                // be it in any function
                for (PSNode *ops : nd->getOperands()) {
                    nodes.insert(ops);
                }
            }
        }

        dumpToDot(nodes, type);

        // dump edges representing procedure calls, so that
        // the graph is conntected
        for (auto nd : nodes) {
            if (nd->getType() == PSNodeType::CALL ||
                nd->getType() == PSNodeType::CALL_FUNCPTR) {
                auto ret = nd->getPairedNode();
                if (ret == nullptr)
                    continue;

                printf("\tNODE%u -> NODE%u [penwidth=2 style=dashed]\n",
                       nd->getID(), ret->getID());

            }
        }
    } else {
        dumpToDot(pta->getPS()->getGlobals(), type);
        dumpToDot(pta->getNodes(), type);
    }

    printf("}\n");
}

static void
dumpPointerGraph(DGLLVMPointerAnalysis *pta, PTType type) {
    assert(pta);

    if (todot)
        dumpPointerGraphdot(pta, type);
    else {
        const auto& nodes = pta->getNodes();
        for (const auto& node : nodes) {
            if (node) // node id 0 is nullptr
                dumpPSNode(node.get(), type);
        }
    }
}

static void dumpStats(DGLLVMPointerAnalysis *pta) {
    const auto& nodes = pta->getNodes();
    printf("Pointer subgraph size: %zu\n", nodes.size()-1);

    size_t nonempty_size = 0; // number of nodes with non-empty pt-set
    size_t maximum = 0; // maximum pt-set size
    size_t pointing_to_unknown = 0;
    size_t pointing_only_to_unknown = 0;
    size_t pointing_to_invalidated = 0;
    size_t pointing_only_to_invalidated = 0;
    size_t singleton_count = 0;
    size_t singleton_nonconst_count = 0;
    size_t pointing_to_heap = 0;
    size_t pointing_to_global = 0;
    size_t pointing_to_stack = 0;
    size_t pointing_to_function = 0;
    size_t has_known_size = 0;
    size_t allocation_num = 0;
    size_t points_to_only_known_size = 0;
    size_t known_size_known_offset = 0;
    size_t only_valid_target = 0;
    size_t only_valid_and_some_known = 0;

    for (auto& node : nodes) {
        if (!node.get())
            continue;

        if (node->pointsTo.size() > 0)
            ++nonempty_size;

        if (node->pointsTo.size() == 1) {
            ++singleton_count;
            if (node->getType() == PSNodeType::CONSTANT ||
                node->getType() == PSNodeType::FUNCTION)
                ++singleton_nonconst_count;
        }

        if (node->pointsTo.size() > maximum)
            maximum = node->pointsTo.size();

        bool _points_to_only_known_size = true;
        bool _known_offset_only = true;
        bool _has_known_size_offset = false;
        bool _has_only_valid_targets = true;
        for (const auto& ptr : node->pointsTo) {
            if (ptr.offset.isUnknown()) {
                _known_offset_only = false;
            }

            if (ptr.isUnknown()) {
                _has_only_valid_targets = false;
                ++pointing_to_unknown;
                if (node->pointsTo.size() == 1)
                    ++pointing_only_to_unknown;
            }

            if (ptr.isInvalidated()) {
                _has_only_valid_targets = false;
                ++pointing_to_invalidated;
                if (node->pointsTo.size() == 1)
                    ++pointing_only_to_invalidated;
            }

            if (ptr.isNull()) {
                _has_only_valid_targets = false;
            }

            auto alloc = PSNodeAlloc::get(ptr.target);
            if (alloc) {
                ++allocation_num;
                if (node->getSize() != 0 &&
                    node->getSize() != Offset::UNKNOWN) {
                    ++has_known_size;
                    if (!ptr.offset.isUnknown())
                        _has_known_size_offset = true;
                } else
                    _points_to_only_known_size = false;

                if (alloc->isHeap()) {
                    ++pointing_to_heap;
                } else if (alloc->isGlobal()) {
                    ++pointing_to_global;
                } else if (alloc->getType() == PSNodeType::ALLOC){
                    assert(!alloc->isGlobal());
                    ++pointing_to_stack;
                }
            } else {
                _points_to_only_known_size = false;;

                if (ptr.target->getType() == PSNodeType::FUNCTION) {
                    ++pointing_to_function;
                }
            }
        }

        if (_points_to_only_known_size) {
            ++points_to_only_known_size;
            if (_known_offset_only)
                ++known_size_known_offset;
        }

        if (_has_only_valid_targets) {
            ++only_valid_target;
            if (_has_known_size_offset)
                ++only_valid_and_some_known;
        }
    }

    printf("Allocations: %zu\n", allocation_num);
    printf("Allocations with known size: %zu\n", has_known_size);
    printf("Nodes with non-empty pt-set: %zu\n", nonempty_size);
    printf("Pointers pointing only to known-size allocations: %zu\n",
            points_to_only_known_size);
    printf("Pointers pointing only to known-size allocations with known offset: %zu\n",
           known_size_known_offset);
    printf("Pointers pointing only to valid targets: %zu\n", only_valid_target);
    printf("Pointers pointing only to valid targets and some known size+offset: %zu\n", only_valid_and_some_known);

    double avg_ptset_size = 0;
    double avg_nonempty_ptset_size = 0; // avg over non-empty sets only
    size_t accumulated_ptset_size = 0;

    for (auto& node : nodes) {
        if (!node.get())
            continue;

        if (accumulated_ptset_size > (~((size_t) 0)) - node->pointsTo.size()) {
            printf("Accumulated points to sets size > 2^64 - 1");
            avg_ptset_size += (accumulated_ptset_size /
                                static_cast<double>(nodes.size()-1));
            avg_nonempty_ptset_size += (accumulated_ptset_size /
                                        static_cast<double>(nonempty_size));
            accumulated_ptset_size = 0;
        }
        accumulated_ptset_size += node->pointsTo.size();
    }

    avg_ptset_size += (accumulated_ptset_size /
                            static_cast<double>(nodes.size()-1));
    avg_nonempty_ptset_size += (accumulated_ptset_size /
                                    static_cast<double>(nonempty_size));
    printf("Average pt-set size: %6.3f\n", avg_ptset_size);
    printf("Average non-empty pt-set size: %6.3f\n", avg_nonempty_ptset_size);
    printf("Pointing to singleton: %zu\n", singleton_count);
    printf("Non-constant pointing to singleton: %zu\n", singleton_nonconst_count);
    printf("Pointing to unknown: %zu\n", pointing_to_unknown);
    printf("Pointing to unknown singleton: %zu\n", pointing_only_to_unknown );
    printf("Pointing to invalidated: %zu\n", pointing_to_invalidated);
    printf("Pointing to invalidated singleton: %zu\n", pointing_only_to_invalidated);
    printf("Pointing to heap: %zu\n", pointing_to_heap);
    printf("Pointing to global: %zu\n", pointing_to_global);
    printf("Pointing to stack: %zu\n", pointing_to_stack);
    printf("Pointing to function: %zu\n", pointing_to_function);
    printf("Maximum pt-set size: %zu\n", maximum);
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
        SMD.print("llvm-pta-compare", llvm::errs());
    }

    return M;
}

#ifndef USING_SANITIZERS
void setupStackTraceOnError(int argc, char *argv[])
{

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 9
    llvm::sys::PrintStackTraceOnErrorSignal();
#else
    llvm::sys::PrintStackTraceOnErrorSignal(llvm::StringRef());
#endif
    llvm::PrettyStackTraceProgram X(argc, argv);

}
#else
void setupStackTraceOnError(int, char **) {}
#endif // not USING_SANITIZERS



int main(int argc, char *argv[]) {
    setupStackTraceOnError(argc, argv);

    SlicerOptions options = parseSlicerOptions(argc, argv,
                                               /* requireCrit = */ false);

    if (enable_debug) {
        DBG_ENABLE();
    }

    if (verbose_more) {
        verbose = true;
    }
    if (callgraph_only) {
        callgraph = true;
    }

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> M = parseModule(context, options);
    if (!M) {
        llvm::errs() << "Failed parsing '" << options.inputFile << "' file:\n";
        return 1;
    }

    if (!display_only.empty()) {
        for (const auto& func : splitList(display_only)) {
            auto llvmFunc = M->getFunction(func);
            if (!llvmFunc) {
                llvm::errs() << "Invalid function to display: " << func
                             << ". Function not found in the module\n";
                return 1;
            }
            display_only_func.push_back(llvmFunc);
        }
    }

    TimeMeasure tm;
    auto& opts = options.dgOptions.PTAOptions;

#ifdef HAVE_SVF
    if (opts.isSVF()) {
        assert(dump_iteration == 0 && "SVF does not support -iteration");
        assert(!dump_graph_only && "SVF does not support -dump_graph_only");
        assert(!dump_graph_only && "SVF does not support -statistics yet");
    }
#endif

    if (!dump_ir) {
        std::unique_ptr<LLVMPointerAnalysis> llvmpta;

#ifdef HAVE_SVF
        if (opts.isSVF())
            llvmpta.reset(new SVFPointerAnalysis(M.get(), opts));
        else
#endif
            llvmpta.reset(new DGLLVMPointerAnalysis(M.get(), opts));

        tm.start();
        llvmpta->run();
        tm.stop();
        tm.report("INFO: Pointer analysis took");

        if (_stats) {
            if (opts.isSVF()) {
                llvm::errs() << "SVF analysis does not support stats dumping\n";
            } else {
                dumpStats(static_cast<DGLLVMPointerAnalysis*>(llvmpta.get()));
            }
            return 0;
        }

        if (_quiet) {
            return 0;
        }

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

        for (auto& F: *M.get()) {
            for (auto& B : F) {
                for (auto& I : B) {
                    if (!I.getType()->isPointerTy() &&
                        !I.getType()->isIntegerTy()) {
                        continue;
                    }

                    if (dump_c_lines && llvm::isa<llvm::AllocaInst>(&I)) {
                        // do not dump I->I for alloca, it makes no sense for C
                        continue;
                    }

                    auto pts = llvmpta->getLLVMPointsTo(&I);
                    if (pts.isUnknownSingleton()) {
                        // do not dump the "no-information"
                        continue;
                    }

                    std::cout << valToStr(&I) << "\n";
                    for (const auto& ptr : pts) {
                        std::cout << "  -> " << valToStr(ptr.value) << "\n";
                    }
                    if (pts.hasUnknown()) {
                        std::cout << "  -> unknown\n";
                    }
                    if (pts.hasNull()) {
                        std::cout << "  -> null\n";
                    }
                    if (pts.hasNullWithOffset()) {
                        std::cout << "  -> null + ?\n";
                    }
                    if (pts.hasInvalidated()) {
                        std::cout << "  -> invalidated\n";
                    }
                }
            }
        }
        return 0;
    }


    ///
    // Dumping the IR of pointer analysis
    //
    DGLLVMPointerAnalysis PTA(M.get(), opts);

    tm.start();

    PTA.initialize();

    if (dump_graph_only) {
        tm.stop();
        tm.report("INFO: Pointer analysis (building graph) took");
        dumpPointerGraph(&PTA, opts.analysisType);
        return 0;
    }

    auto PA = PTA.getPTA();
    assert(PA && "Did not initialize the analysis");

    // run the analysis
    if (dump_iteration > 0) {
        // do preprocessing and queue the nodes
        PA->preprocess();
        PA->initialize_queue();

        // do fixpoint
        for (unsigned i = 0; i < dump_iteration; ++i) {
            if (PA->iteration() == false)
                break;
            PA->queue_changed();
        }
    } else {
        PA->run();
    }

    tm.stop();
    tm.report("INFO: Pointer analysis took");

    if (_stats) {
        dumpStats(&PTA);
    }

    if (_quiet)
        return 0;

    dumpPointerGraph(&PTA, opts.analysisType);

    return 0;
}
