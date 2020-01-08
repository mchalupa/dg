#include <set>
#include <vector>
#include <string>
#include <cassert>
#include <iostream>
#include <fstream>

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

#include "dg/llvm/SystemDependenceGraph/SDG2Dot.h"
#include "dg/llvm/SystemDependenceGraph/AnnotationWriter.h"
#include "dg/util/debug.h"

using namespace dg;

using llvm::errs;

llvm::cl::opt<bool> enable_debug("dbg",
    llvm::cl::desc("Enable debugging messages (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dump_bb_only("dump-bb-only",
    llvm::cl::desc("Only dump basic blocks of dependence graph to dot"
                   " (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

class SDGDumper {
    const SlicerOptions& options;
    llvmdg::SystemDependenceGraph *dg;
    bool bb_only{false};
    uint32_t dump_opts{0};//{debug::PRINT_DD | debug::PRINT_CD | debug::PRINT_USE | debug::PRINT_ID};

public:
    SDGDumper(const SlicerOptions& opts,
              llvmdg::SystemDependenceGraph *dg,
              bool bb_only = false,
              uint32_t dump_opts = 0)
    : options(opts), dg(dg), bb_only(bb_only), dump_opts(dump_opts) {}

    void dumpToDot(const char *suffix = nullptr) {
        // compose new name
        std::string fl(options.inputFile);
        if (suffix)
            replace_suffix(fl, suffix);
        else
            replace_suffix(fl, ".dot");

        errs() << "Dumping SDG to " << fl << "\n";

        if (bb_only) {
            assert(false && "Not implemented");
            //SDGDumpBlocks dumper(dg, dump_opts, fl.c_str());
            //dumper.dump();
        } else {
            llvmdg::SDG2Dot dumper(dg);
            dumper.dump(fl.c_str());
        }
    }
};


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
        SMD.print("llvm-slicer", llvm::errs());
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

int main(int argc, char *argv[])
{
    setupStackTraceOnError(argc, argv);

    SlicerOptions options = parseSlicerOptions(argc, argv,
                                               /* requireCrit = */ false);

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

    DGLLVMPointerAnalysis PTA(M.get());
    PTA.run();
    LLVMDataDependenceAnalysis DDA(M.get(), &PTA);
    DDA.run();

    llvmdg::SystemDependenceGraph sdg(M.get(), &PTA);

    SDGDumper dumper(options, &sdg, dump_bb_only);
    dumper.dumpToDot();

    return 0;
}
