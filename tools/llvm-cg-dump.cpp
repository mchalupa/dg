#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "dg/tools/llvm-slicer-opts.h"
#include "dg/tools/llvm-slicer-utils.h"
#include "dg/tools/llvm-slicer.h"

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#ifdef HAVE_SVF
#include "dg/llvm/PointerAnalysis/SVFPointerAnalysis.h"
#endif
#include "dg/llvm/CallGraph/CallGraph.h"
#include "dg/llvm/PointerAnalysis/DGPointerAnalysis.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/util/debug.h"

using namespace dg;

using llvm::errs;

llvm::cl::opt<bool> enable_debug(
        "dbg", llvm::cl::desc("Enable debugging messages (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> usepta("use-pta",
                           llvm::cl::desc("Use points analysis to build CG."),
                           llvm::cl::init(true), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> lazy("lazy-cg",
                         llvm::cl::desc("Use the LazyLLVMCallGraph."),
                         llvm::cl::init(true), llvm::cl::cat(SlicingOpts));

static void dumpCallGraph(llvmdg::CallGraph &CG) {
    std::cout << "digraph CallGraph {\n";

    for (const auto *f : CG.functions()) {
        for (const auto *c : CG.callees(f)) {
            std::cout << "  \"" << f->getName().str() << "\" -> \""
                      << c->getName().str() << "\"\n";
        }
    }

    std::cout << "}\n";
}

int main(int argc, char *argv[]) {
    setupStackTraceOnError(argc, argv);
    SlicerOptions options = parseSlicerOptions(argc, argv);

    if (enable_debug) {
        DBG_ENABLE();
    }

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> M =
            parseModule("llvm-cg-dump", context, options);
    if (!M)
        return 1;

    if (!M->getFunction(options.dgOptions.entryFunction)) {
        llvm::errs() << "The entry function not found: "
                     << options.dgOptions.entryFunction << "\n";
        return 1;
    }

    if (usepta) {
        auto &ptaopts = options.dgOptions.PTAOptions;
#ifdef HAVE_SVF
        if (ptaopts.isSVF()) {
            SVFPointerAnalysis PTA(M.get(), ptaopts);
            PTA.run();

            llvmdg::CallGraph CG(M.get(), &PTA, lazy);
            dumpCallGraph(CG);
        } else
#endif // HAVE_SVF
        {
            DGLLVMPointerAnalysis PTA(M.get(), ptaopts);
            PTA.run();

            if (lazy) {
                llvmdg::CallGraph CG(M.get(), &PTA, lazy);
                CG.build();
                dumpCallGraph(CG);
            } else {
                // re-use the call-graph from PTA
                llvmdg::CallGraph CG(PTA.getPTA()->getPG()->getCallGraph());
                dumpCallGraph(CG);
            }
        }
    } else {
        if (!lazy) {
            llvm::errs() << "Can build CG without PTA only with -lazy option\n";
            return 1;
        }

        llvmdg::CallGraph CG(M.get());
        CG.build();
        dumpCallGraph(CG);
    }

    return 0;
}
