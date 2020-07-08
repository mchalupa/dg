#include <cassert>
#include <iostream>
#include <ctime>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/Config/llvm-config.h>

#if (LLVM_VERSION_MAJOR < 3)
#error "Unsupported version of LLVM"
#endif

#include "llvm-slicer.h"
#include "llvm-slicer-opts.h"
#include "llvm-slicer-utils.h"

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

//#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/InstIterator.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#ifdef HAVE_SVF
#include "dg/llvm/PointerAnalysis/SVFPointerAnalysis.h"
#endif
#include "dg/llvm/PointerAnalysis/DGPointerAnalysis.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "dg/util/debug.h"
#include "dg/ADT/Queue.h"

#include "ControlDependence/CDGraph.h"
#include "llvm/ControlDependence/NTSCD.h"
#include "llvm/ControlDependence/DOD.h"

using namespace dg;

using llvm::errs;

llvm::cl::opt<bool> enable_debug("dbg",
    llvm::cl::desc("Enable debugging messages (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> quiet("q",
    llvm::cl::desc("Do not generate output, just run the analysis "
                   "(e.g., for performance analysis) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> total_only("total-only",
    llvm::cl::desc("Do not generate output other than the total time (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> fun_info_only("fun-info-only",
    llvm::cl::desc("Only dump statistics about the functions in module (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

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
        SMD.print("llvm-cda-bench", llvm::errs());
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

static inline bool hasSuccessors(const llvm::BasicBlock *B) {
    return succ_begin(B) != succ_end(B);
}

static void dumpFunStats(const llvm::Function& F) {
    unsigned instrs = 0, branches = 0, blinds = 0;
    std::cout << "Function '" << F.getName().str() << "'\n";
    for (auto& B : F) {
        instrs += B.size();
        auto n = 0;
        for (auto *s: successors(&B)) {
            (void)s;
            ++n;
        }
        if (n == 0)
            ++blinds;
        if (n > 1)
            ++branches;
    }
    std::cout << "  bblocks: " << F.size() << "\n";
    std::cout << "  instructions: " << instrs << "\n";
    std::cout << "  branches: " << branches << "\n";
    std::cout << "  blind ends: " << blinds << "\n";

    // visited, on_stack
    std::map<const llvm::BasicBlock *, bool> on_stack;
    unsigned backedges = 0;
    unsigned tree = 0;
    unsigned nontree = 0;
    unsigned forward = 0;
    size_t maxdepth = 0;
    struct StackNode {
        const llvm::BasicBlock *block;
        // next successor to follow
        const llvm::BasicBlock *next_succ{nullptr};

        StackNode(const llvm::BasicBlock *b,
                  const llvm::BasicBlock *s = nullptr)
        : block(b), next_succ(s) {}
    };

    std::vector<StackNode> stack;
    on_stack[&F.getEntryBlock()] = true;

    stack.push_back({&F.getEntryBlock(),
                     hasSuccessors(&F.getEntryBlock()) ?
                       *succ_begin(&F.getEntryBlock()) : nullptr});
    maxdepth = 1;

    while (!stack.empty()) {
        auto& si = stack.back();
        assert(si.block);
        auto *nextblk = si.next_succ;
        // set next successor
        if (!nextblk) {
            on_stack[si.block] = false;
            stack.pop_back();
            continue;
        }

        auto it = succ_begin(si.block);
        auto et = succ_end(si.block);
        while (it != et && *it != si.next_succ) {
            ++it;
        }

        assert(*it == si.next_succ);
        // can have multiple same successors
        auto sit = on_stack.find(*it);
        bool is_on_stack = false;
        if (sit != on_stack.end()) {
            is_on_stack = sit->second;
        }
        while (it != et && *it == si.next_succ) {
            // XXX: we may loose some back/forward edges here
            // (we do not count the multiplicity)
            ++it;
        }
        if (it == et) {
            si.next_succ = nullptr;
        } else {
            si.next_succ = *it;
        }

        sit = on_stack.find(nextblk);
        if (sit != on_stack.end()) {
            // we have already visited this node
            ++nontree;
            if (sit->second) { // still on stack
                ++backedges;
            } else {
                ++forward;
            }
            // backtrack
            sit->second = false;
            stack.pop_back();
        } else {
            ++tree;
            on_stack[nextblk] = true;
            stack.push_back({nextblk,
                             hasSuccessors(nextblk) ?
                               *succ_begin(nextblk) : nullptr});
            maxdepth = std::max(maxdepth, stack.size());
        }
    }

    std::cout << "  DFS tree edges: " << tree << "\n";
    std::cout << "  DFS nontree edges: " << nontree << "\n";
    std::cout << "  DFS forward: " << forward << "\n";
    std::cout << "  DFS backedges: " << backedges << "\n";
    std::cout << "  DFS max depth: " << maxdepth << "\n";
}

int main(int argc, char *argv[])
{
    setupStackTraceOnError(argc, argv);

    SlicerOptions options = parseSlicerOptions(argc, argv,
                                               /* requireCrit = */ false);

    if (enable_debug) {
        DBG_ENABLE();
    }

    if (total_only) {
        quiet = true;
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

    if (fun_info_only) {
        for (auto& F : *M.get()) {
            if (F.isDeclaration()) {
                continue;
            }

            dumpFunStats(F);
        }
        return 0;
    }

    clock_t start, end, elapsed, total = 0;
    LLVMControlDependenceAnalysis cda(M.get(), options.dgOptions.CDAOptions);

    for (auto& F : *M.get()) {
        if (F.isDeclaration()) {
            continue;
        }
        start = clock();
        cda.compute(&F); // compute all the information
        end = clock();
        elapsed = end - start;
        total += elapsed;

        if (quiet)
            continue;

        dumpFunStats(F);
        std::cout << "Elapsed time: "
                  << static_cast<float>(elapsed) / CLOCKS_PER_SEC << " s ("
                  << elapsed << " ticks)\n";
        std::cout << "-----" << std::endl;
    }

    if (!quiet || total_only) {
        std::cout << "Total elapsed time: "
                  << static_cast<float>(total) / CLOCKS_PER_SEC << " s ("
                  << total << " ticks)" << std::endl;
    }

    return 0;
}
