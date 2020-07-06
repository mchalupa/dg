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

    clock_t start, end, elapsed, total = 0;
    LLVMControlDependenceAnalysis cda(M.get(), options.dgOptions.CDAOptions);

    for (auto& F : *M.get()) {
        if (F.isDeclaration()) {
            continue;
        }
        start = clock();
        cda.compute(); // compute all the information
        end = clock();
        elapsed = end - start;
        total += elapsed;

        if (quiet)
            continue;

        dumpFunStats(F);
        std::cout << "Elapsed time: "
                  << static_cast<float>(elapsed) / CLOCKS_PER_SEC << " s ("
                  << elapsed << " ticks)\n";
        std::cout << "-----\n";
    }

    if (!quiet || total_only) {
        std::cout << "Total elapsed time: "
                  << static_cast<float>(total) / CLOCKS_PER_SEC << " s ("
                  << total << " ticks)\n";
    }

    return 0;
}
