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
#include <cstdio>
#include <cstdlib>

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

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"
#include "dg/analysis/PointsTo/PointerAnalysisFI.h"
#include "dg/analysis/PointsTo/PointerAnalysisFS.h"
#include "dg/analysis/PointsTo/PointerAnalysisFSInv.h"
#include "dg/analysis/PointsTo/Pointer.h"

#include "TimeMeasure.h"

using namespace dg;
using namespace dg::analysis::pta;
using dg::debug::TimeMeasure;
using llvm::errs;

static bool verbose;
static bool verbose_more;
static bool ids_only = false;
static bool threads = false;
static bool dump_graph_only = false;
static bool names_with_funs = false;
static bool callgraph = false;
static uint64_t dump_iteration = 0;
static const char *entry_func = "main";

static char *display_only = nullptr;
static std::vector<const llvm::Function *> display_only_func;

std::unique_ptr<PointerAnalysis> PA;

enum PTType {
    FLOW_SENSITIVE = 1,
    FLOW_INSENSITIVE,
    WITH_INVALIDATE,
};

static std::string
getInstName(const llvm::Value *val)
{
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

static void
printName(PSNode *node, bool dot = false)
{
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

static void dumpPointer(const Pointer& ptr, bool dot)
{
    printName(ptr.target, dot);

    if (ptr.offset.isUnknown())
        printf(" + UNKNOWN");
    else
        printf(" + %lu", *ptr.offset);
}

static void
dumpMemoryObject(MemoryObject *mo, int ind, bool dot)
{
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
                    width = printf("[%lu]", *it.first);

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
dumpMemoryMap(PointerAnalysisFS::MemoryMapT *mm, int ind, bool dot)
{
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

static bool mmChanged(PSNode *n)
{
    if (n->predecessorsNum() == 0)
        return true;

    PointerAnalysisFS::MemoryMapT *mm
        = n->getData<PointerAnalysisFS::MemoryMapT>();

    for (PSNode *pred : n->getPredecessors()) {
        if (pred->getData<PointerAnalysisFS::MemoryMapT>() != mm)
            return true;
    }

    return false;
}

static void
dumpPointerGraphData(PSNode *n, PTType type, bool dot = false)
{
    assert(n && "No node given");
    if (type == FLOW_INSENSITIVE) {
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
dumpPSNode(PSNode *n, PTType type)
{
    printf("NODE %3u: ", n->getID());
    printName(n);

    PSNodeAlloc *alloc = PSNodeAlloc::get(n);
    if (alloc &&
        (alloc->getSize() || alloc->isHeap() || alloc->isZeroInitialized()))
        printf(" [size: %lu, heap: %u, zeroed: %u]",
               alloc->getSize(), alloc->isHeap(), alloc->isZeroInitialized());

    printf(" (points-to size: %lu)\n", n->pointsTo.size());

    for (const Pointer& ptr : n->pointsTo) {
        printf("    -> ");
        printName(ptr.target, false);
        if (ptr.offset.isUnknown())
            puts(" + Offset::UNKNOWN");
        else
            printf(" + %lu\n", *ptr.offset);
    }
    if (verbose) {
        dumpPointerGraphData(n, type);
    }
}

static void
dumpNodeToDot(PSNode *node, PTType type)
{
    printf("\tNODE%u [label=\"<%u> ", node->getID(), node->getID());
    printPSNodeType(node->getType());
    printf("\\n");
    printName(node, true);
    printf("\\nparent: %u\\n", node->getParent() ? node->getParent()->getID() : 0);

    PSNodeAlloc *alloc = PSNodeAlloc::get(node);
    if (alloc && (alloc->getSize() || alloc->isHeap() || alloc->isZeroInitialized()))
        printf("\\n[size: %lu, heap: %u, zeroed: %u]",
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
            printf("%lu", *ptr.offset);
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

static void
dumpNodeEdgesToDot(PSNode *node)
{
    for (PSNode *succ : node->getSuccessors()) {
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
dumpToDot(const ContT& nodes, PTType type)
{
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


static std::vector<std::string> splitList(const std::string& opt, char sep = ',')
{
    std::vector<std::string> ret;
    if (opt.empty())
        return ret;

    size_t old_pos = 0;
    size_t pos = 0;
    while (true) {
        old_pos = pos;

        pos = opt.find(sep, pos);
        ret.push_back(opt.substr(old_pos, pos - old_pos));

        if (pos == std::string::npos)
            break;
        else
            ++pos;
    }

    return ret;
}

static void
dumpPointerGraphdot(DGLLVMPointerAnalysis *pta, PTType type)
{

    printf("digraph \"Pointer State Subgraph\" {\n");

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

    if (callgraph) {
        // dump call-graph
        const auto& CG = pta->getPS()->getCallGraph();
        for (auto& it : CG) {
            printf("NODEcg%u [label=\"%s\"]\n",
                    it.second.getID(),
                    PSNodeEntry::get(it.first)->getFunctionName().c_str());
        }
        for (auto& it : CG) {
            for (auto succ : it.second.getCalls()) {
                printf("NODEcg%u -> NODEcg%u\n", it.second.getID(), succ->getID());
            }
        }
    }

    printf("}\n");
}

static void
dumpPointerGraph(DGLLVMPointerAnalysis *pta, PTType type, bool todot)
{
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

static void
dumpStats(DGLLVMPointerAnalysis *pta)
{
    const auto& nodes = pta->getNodes();
    printf("Pointer subgraph size: %lu\n", nodes.size()-1);

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

    printf("Allocations: %lu\n", allocation_num);
    printf("Allocations with known size: %lu\n", has_known_size);
    printf("Nodes with non-empty pt-set: %lu\n", nonempty_size);
    printf("Pointers pointing only to known-size allocations: %lu\n",
            points_to_only_known_size);
    printf("Pointers pointing only to known-size allocations with known offset: %lu\n",
           known_size_known_offset);
    printf("Pointers pointing only to valid targets: %lu\n", only_valid_target);
    printf("Pointers pointing only to valid targets and some known size+offset: %lu\n", only_valid_and_some_known);

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
    printf("Pointing to singleton: %lu\n", singleton_count);
    printf("Non-constant pointing to singleton: %lu\n", singleton_nonconst_count);
    printf("Pointing to unknown: %lu\n", pointing_to_unknown);
    printf("Pointing to unknown singleton: %lu\n", pointing_only_to_unknown );
    printf("Pointing to invalidated: %lu\n", pointing_to_invalidated);
    printf("Pointing to invalidated singleton: %lu\n", pointing_only_to_invalidated);
    printf("Pointing to heap: %lu\n", pointing_to_heap);
    printf("Pointing to global: %lu\n", pointing_to_global);
    printf("Pointing to stack: %lu\n", pointing_to_stack);
    printf("Pointing to function: %lu\n", pointing_to_function);
    printf("Maximum pt-set size: %lu\n", maximum);
}

int main(int argc, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    bool todot = false;
    bool stats = false;
    const char *module = nullptr;
    PTType type = FLOW_INSENSITIVE;
    uint64_t field_senitivity = Offset::UNKNOWN;

    // parse options
    for (int i = 1; i < argc; ++i) {
        // run given points-to analysis
        if (strcmp(argv[i], "-pta") == 0) {
            if (strcmp(argv[i+1], "fs") == 0)
                type = FLOW_SENSITIVE;
            else if (strcmp(argv[i+1], "inv") == 0)
                type = WITH_INVALIDATE;
        } else if (strcmp(argv[i], "-pta-field-sensitive") == 0) {
            field_senitivity = static_cast<uint64_t>(atoll(argv[i + 1]));
        } else if (strcmp(argv[i], "-dot") == 0) {
            todot = true;
        } else if (strcmp(argv[i], "-threads") == 0) {
            threads = true;
        } else if (strcmp(argv[i], "-callgraph") == 0) {
            callgraph = true;
        } else if (strcmp(argv[i], "-ids-only") == 0) {
            ids_only = true;
        } else if (strcmp(argv[i], "-iteration") == 0) {
            dump_iteration = static_cast<uint64_t>(atoll(argv[i + 1]));
        } else if (strcmp(argv[i], "-graph-only") == 0) {
            dump_graph_only = true;
        } else if (strcmp(argv[i], "-names-with-funs") == 0) {
            names_with_funs = true;
        } else if (strcmp(argv[i], "-stats") == 0) {
            stats = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-vv") == 0) {
            verbose = true;
            verbose_more = true;
        } else if (strcmp(argv[i], "-entry") == 0) {
            entry_func = argv[i + 1];
        } else if (strcmp(argv[i], "-display-only") == 0) {
            display_only = argv[i + 1];
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module [output_file]\n";
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

    TimeMeasure tm;
    if (display_only) {
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

    DGLLVMPointerAnalysis PTA(M, entry_func, field_senitivity, threads);

    tm.start();

    // use createAnalysis instead of the run() method so that we won't delete
    // the analysis data (like memory objects) which may be needed
    if (type == FLOW_INSENSITIVE) {
        PA = std::unique_ptr<PointerAnalysis>(
            PTA.createPTA<analysis::pta::PointerAnalysisFI>()
            );
    } else if (type == WITH_INVALIDATE) {
        PA = std::unique_ptr<PointerAnalysis>(
            PTA.createPTA<analysis::pta::PointerAnalysisFSInv>()
            );
    } else {
        PA = std::unique_ptr<PointerAnalysis>(
            PTA.createPTA<analysis::pta::PointerAnalysisFS>()
            );
    }

    if (dump_graph_only) {
        dumpPointerGraph(&PTA, type, true);
        return 0;
    }

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
    tm.report("INFO: Points-to analysis [new] took");

    if (stats) {
        dumpStats(&PTA);
        return 0;
    }

    dumpPointerGraph(&PTA, type, todot);

    return 0;
}
