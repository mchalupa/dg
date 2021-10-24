#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include <cassert>
#include <cstdio>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"
#include "dg/PointerAnalysis/PointerAnalysisFSInv.h"
#include "dg/llvm/DataDependence/DataDependence.h"
#include "dg/llvm/LLVMDG2Dot.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMDependenceGraphBuilder.h"
#include "dg/llvm/LLVMSlicer.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "dg/tools/llvm-slicer-opts.h"
#include "dg/tools/llvm-slicer-utils.h"

#include "dg/util/TimeMeasure.h"

using namespace dg;
using namespace dg::debug;
using llvm::errs;

llvm::cl::opt<bool> enable_debug(
        "dbg", llvm::cl::desc("Enable debugging messages (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> bb_only(
        "bb-only",
        llvm::cl::desc("Only dump basic blocks of dependence graph to dot"
                       " (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> mark_only(
        "mark",
        llvm::cl::desc("Only mark nodes that are going to be in the slice"
                       " (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<std::string>
        dump_func_only("func", llvm::cl::desc("Only dump a given function."),
                       llvm::cl::value_desc("string"), llvm::cl::init(""),
                       llvm::cl::cat(SlicingOpts));

// TODO: This machinery can be replaced with llvm::cl::callback setting
// the desired flags directly when we drop support for LLVM 9 and older.
enum PrintingOpts {
    call,
    cfgall,
    postdom,
    no_cfg,
    no_control,
    no_data,
    no_use
};

llvm::cl::list<PrintingOpts> print_opts(
        llvm::cl::desc("Dot printer options:"),
        llvm::cl::values(
                clEnumVal(call, "Print calls (default=false)."),
                clEnumVal(cfgall,
                          "Print full control flow graph (default=false)."),
                clEnumVal(postdom,
                          "Print post dominator tree (default=false)."),
                clEnumValN(no_cfg, "no-cfg",
                           "Do not print control flow graph (default=false)."),
                clEnumValN(
                        no_control, "no-control",
                        "Do not print control dependencies (default=false)."),
                clEnumValN(no_data, "no-data",
                           "Do not print data dependencies (default=false)."),
                clEnumValN(no_use, "no-use",
                           "Do not print uses (default=false).")
#if LLVM_VERSION_MAJOR < 4
                        ,
                nullptr
#endif
                ),
        llvm::cl::cat(SlicingOpts));

int main(int argc, char *argv[]) {
    setupStackTraceOnError(argc, argv);
    SlicerOptions options = parseSlicerOptions(argc, argv);

    uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD | PRINT_USE | PRINT_ID;
    for (auto opt : print_opts) {
        switch (opt) {
        case no_control:
            opts &= ~PRINT_CD;
            break;
        case no_use:
            opts &= ~PRINT_USE;
            break;
        case no_data:
            opts &= ~PRINT_DD;
            break;
        case no_cfg:
            opts &= ~PRINT_CFG;
            break;
        case call:
            opts |= PRINT_CALL;
            break;
        case postdom:
            opts |= PRINT_POSTDOM;
            break;
        case cfgall:
            opts |= PRINT_CFG | PRINT_REV_CFG;
            break;
        }
    }

    if (enable_debug) {
        DBG_ENABLE();
    }

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> M =
            parseModule("llvm-dg-dump", context, options);
    if (!M)
        return 1;

    llvmdg::LLVMDependenceGraphBuilder builder(M.get(), options.dgOptions);
    auto dg = builder.build();

    std::set<LLVMNode *> callsites;
    const std::string &slicingCriteria = options.slicingCriteria;
    if (!slicingCriteria.empty()) {
        const char *sc[] = {slicingCriteria.c_str(), "klee_assume", nullptr};

        dg->getCallSites(sc, &callsites);

        llvmdg::LLVMSlicer slicer;

        if (slicingCriteria == "ret") {
            if (mark_only)
                slicer.mark(dg->getExit());
            else
                slicer.slice(dg.get(), dg->getExit());
        } else {
            if (callsites.empty()) {
                errs() << "ERR: slicing criterion not found: "
                       << slicingCriteria << "\n";
                exit(1);
            }

            uint32_t slid = 0;
            for (LLVMNode *start : callsites)
                slid = slicer.mark(start, slid);

            if (!mark_only)
                slicer.slice(dg.get(), nullptr, slid);
        }

        if (!mark_only) {
            std::string fl(options.inputFile);
            fl.append(".sliced");
            std::ofstream ofs(fl);
            llvm::raw_os_ostream output(ofs);

            SlicerStatistics &st = slicer.getStatistics();
            errs() << "INFO: Sliced away " << st.nodesRemoved << " from "
                   << st.nodesTotal << " nodes\n";

#if LLVM_VERSION_MAJOR > 6
            llvm::WriteBitcodeToFile(*M, output);
#else
            llvm::WriteBitcodeToFile(M.get(), output);
#endif
        }
    }
    const char *only_func = nullptr;
    if (!dump_func_only.empty())
        only_func = dump_func_only.c_str();

    if (bb_only) {
        LLVMDGDumpBlocks dumper(dg.get(), opts);
        dumper.dump(nullptr, only_func);
    } else {
        LLVMDG2Dot dumper(dg.get(), opts);
        dumper.dump(nullptr, only_func);
    }

    return 0;
}
