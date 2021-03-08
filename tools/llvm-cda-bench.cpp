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

#include "dg/tools/llvm-slicer.h"
#include "dg/tools/llvm-slicer-opts.h"
#include "dg/tools/llvm-slicer-utils.h"

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 7
#include <llvm/IR/LLVMContext.h>
#endif
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/InstIterator.h>
SILENCE_LLVM_WARNINGS_POP

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

llvm::cl::opt<bool> scd("scd",
    llvm::cl::desc("Benchmark standard CD (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> ntscd("ntscd",
    llvm::cl::desc("Benchmark NTSCD (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> ntscd2("ntscd2",
    llvm::cl::desc("Benchmark NTSCD 2 (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> ntscd_ranganath("ntscd-ranganath",
    llvm::cl::desc("Benchmark NTSCD (Ranganath algorithm) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> ntscd_ranganath_orig("ntscd-ranganath-orig",
    llvm::cl::desc("Benchmark NTSCD (Ranganath original - wrong - algorithm) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> ntscd_legacy("ntscd-legacy",
    llvm::cl::desc("Benchmark NTSCD (legacy implementation) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dod("dod",
    llvm::cl::desc("Benchmark DOD (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dod_ranganath("dod-ranganath",
    llvm::cl::desc("Benchmark DOD (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dod_ntscd("dod+ntscd",
    llvm::cl::desc("Benchmark DOD + NTSCD (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> scc("scc",
    llvm::cl::desc("Strong control closure (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> compare("compare",
    llvm::cl::desc("Compare the resulting control dependencies (default=false)."),
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

void compareResults(const std::set<std::pair<const llvm::Value *, const llvm::Value *>>& R1,
                    const std::set<std::pair<const llvm::Value *, const llvm::Value *>>& R2,
                    const std::string& A1, const std::string& A2,
                    const llvm::Function& F) {
    std::cout << "In function '" << F.getName().str() << "'\n";
    std::cout << " " << A1 << " computed " << R1.size() << " dependencies\n";
    std::cout << " " << A2 << " computed " << R2.size() << " dependencies\n";
    std::cout << "-----\n";

    size_t a1has = 0, a2has = 0;
    for (auto& d : R1) {
        if (R2.count(d) == 0) {
            ++a1has;
        }
    }
    for (auto& d : R2) {
        if (R1.count(d) == 0) {
            ++a2has;
        }
    }

    if (a1has > 0 || a2has > 0) {
        std::cout << " " << A1 << " has " << a1has << " that are not in " << A2 << "\n";
        std::cout << " " << A2 << " has " << a2has << " that are not in " << A1 << std::endl;
    }
}

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

static inline std::unique_ptr<LLVMControlDependenceAnalysis>
createAnalysis(llvm::Module *M, const LLVMControlDependenceAnalysisOptions& opts) {
    return std::unique_ptr<LLVMControlDependenceAnalysis>(
	new LLVMControlDependenceAnalysis(M, opts)
    );
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

    if (fun_info_only) {
        for (auto& F : *M.get()) {
            if (F.isDeclaration()) {
                continue;
            }

            dumpFunStats(F);
        }
        return 0;
    }

    std::vector<std::tuple<std::string,
                          std::unique_ptr<LLVMControlDependenceAnalysis>,
                          size_t>> analyses;

    clock_t start, end, elapsed;
    auto& opts = options.dgOptions.CDAOptions;
    if (scd) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::STANDARD;
        analyses.emplace_back("scd", createAnalysis(M.get(), opts), 0);
    }
    if (ntscd) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD;
        analyses.emplace_back("ntscd", createAnalysis(M.get(), opts), 0);
    }
    if (ntscd2) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD2;
        analyses.emplace_back("ntscd2", createAnalysis(M.get(), opts), 0);
    }
    if (ntscd_ranganath) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD_RANGANATH;
        analyses.emplace_back("ntscd-ranganath", createAnalysis(M.get(), opts), 0);
    }
    if (ntscd_ranganath_orig) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD_RANGANATH_ORIG;
        analyses.emplace_back("ntscd-ranganath-wrong", createAnalysis(M.get(), opts), 0);
    }
    if (ntscd_legacy) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD_LEGACY;
        analyses.emplace_back("ntscd-legacy", createAnalysis(M.get(), opts), 0);
    }
    if (dod) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::DOD;
        analyses.emplace_back("dod", createAnalysis(M.get(), opts), 0);
    }
    if (dod_ranganath) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::DOD_RANGANATH;
        analyses.emplace_back("dod-ranganath", createAnalysis(M.get(), opts), 0);
    }
    if (dod_ntscd) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::DODNTSCD;
        analyses.emplace_back("dod+ntscd", createAnalysis(M.get(), opts), 0);
    }
    if (scc) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::STRONG_CC;
        analyses.emplace_back("scc", createAnalysis(M.get(), opts), 0);
    }

    if (analyses.empty()) {
        std::cerr << "Warning: No analysis to run specified, "
                     "dumping just info about funs\n";
    }

    for (auto& F : *M.get()) {
        if (F.isDeclaration()) {
            continue;
        }

        if (!quiet) {
            dumpFunStats(F);
            std::cout << "Elapsed time: \n";
        }

        for (auto& it : analyses) {
            start = clock();
            std::get<1>(it)->compute(&F); // compute all the information
            end = clock();
            elapsed = end - start;
            std::get<2>(it) += elapsed;
            if (!quiet) {
                std::cout << "  " << std::get<0>(it) << ": "
                          << static_cast<float>(elapsed) / CLOCKS_PER_SEC << " s ("
                          << elapsed << " ticks)\n";
            }
        }
        if (!quiet) {
            std::cout << "-----" << std::endl;
        }
    }

    if (!quiet || total_only) {
        std::cout << "Total elapsed time:\n";
        for (auto& it : analyses) {
            std::cout << "  " << std::get<0>(it) << ": "
                      << static_cast<float>(std::get<2>(it)) / CLOCKS_PER_SEC << " s ("
                      << std::get<2>(it) << " ticks)" << std::endl;
        }
    }

    // compare the results if requested
    if (!compare)
        return 0;

    std::cout << "\n ==== Comparison ====\n";
    for (auto& F : *M.get()) {
        if (F.isDeclaration()) {
            continue;
        }

        // not very efficient...
        std::vector<std::set<std::pair<const llvm::Value *, const llvm::Value *>>> results;
        results.resize(analyses.size());
        unsigned n = 0;
        for (auto& it : analyses) {
            auto *cda = std::get<1>(it).get();
            for (auto& B : F) {
                for (auto *d : cda->getDependencies(&B)) {
                    results[n].emplace(d, &B);
                }

                for (auto& I : B) {
                    for (auto *d : cda->getDependencies(&I)) {
                        results[n].emplace(d, &I);
                    }
                }
            }

            ++n;
        }

        n = 0;
        for (n = 0; n < results.size(); ++n) {
            for (unsigned m = 0; m < n; ++m) {
                compareResults(results[n], results[m],
                               std::get<0>(analyses[n]),  std::get<0>(analyses[m]),
                               F);
            }
        }
    }

    return 0;
}
