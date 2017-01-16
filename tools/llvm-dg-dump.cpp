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

#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
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

#include "llvm/LLVMDependenceGraph.h"
#include "llvm/Slicer.h"
#include "llvm/LLVMDG2Dot.h"
#include "TimeMeasure.h"

#include "llvm/analysis/old/PointsTo.h"
#include "llvm/analysis/old/ReachingDefs.h"
#include "llvm/analysis/old/DefUse.h"

#include "llvm/analysis/DefUse.h"
#include "llvm/analysis/PointsTo/PointsTo.h"
#include "llvm/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "analysis/PointsTo/PointsToFlowSensitive.h"
#include "analysis/PointsTo/PointsToFlowInsensitive.h"

using namespace dg;
using llvm::errs;

int main(int argc, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    bool mark_only = false;
    bool bb_only = false;
    const char *module = nullptr;
    const char *slicing_criterion = nullptr;
    const char *dump_func_only = nullptr;
    const char *pts = "fi";
    CD_ALG cd_alg = CLASSIC;

    using namespace debug;
    uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-no-control") == 0) {
            opts &= ~PRINT_CD;
        } else if (strcmp(argv[i], "-pta") == 0) {
            pts = argv[++i];
        } else if (strcmp(argv[i], "-no-data") == 0) {
            opts &= ~PRINT_DD;
        } else if (strcmp(argv[i], "-nocfg") == 0) {
            opts &= ~PRINT_CFG;
        } else if (strcmp(argv[i], "-call") == 0) {
            opts |= PRINT_CALL;
        } else if (strcmp(argv[i], "-postdom") == 0) {
            opts |= PRINT_POSTDOM;
        } else if (strcmp(argv[i], "-bb-only") == 0) {
            bb_only = true;
        } else if (strcmp(argv[i], "-cfgall") == 0) {
            opts |= PRINT_CFG;
            opts |= PRINT_REV_CFG;
        } else if (strcmp(argv[i], "-func") == 0) {
            dump_func_only = argv[++i];
        } else if (strcmp(argv[i], "-slice") == 0) {
            slicing_criterion = argv[++i];
        } else if (strcmp(argv[i], "-mark") == 0) {
            mark_only = true;
            slicing_criterion = argv[++i];
        } else if (strcmp(argv[i], "-cd-alg") == 0) {
            const char *arg = argv[++i];
            if (strcmp(arg, "classic") == 0)
                cd_alg = CLASSIC;
            else if (strcmp(arg, "ce") == 0)
                cd_alg = CONTROL_EXPRESSION;
            else {
                errs() << "Invalid control dependencies algorithm, try: classic, ce\n";
                abort();
            }

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

    debug::TimeMeasure tm;

    // TODO refactor the code...
    LLVMDependenceGraph d;
    LLVMPointerAnalysis *PTA = new LLVMPointerAnalysis(M);
    if (strcmp(pts, "old")) {
        // new analyses
        if (strcmp(pts, "fs") == 0) {
            tm.start();
            PTA->run<analysis::pta::PointsToFlowSensitive>();
            tm.stop();
        } else if (strcmp(pts, "fi") == 0) {
            tm.start();
            PTA->run<analysis::pta::PointsToFlowInsensitive>();
            tm.stop();
        } else {
            llvm::errs() << "Unknown points to analysis, try: fs, fi\n";
            abort();
        }

        tm.report("INFO: Points-to analysis took");

        d.build(M, PTA);
    } else {
        d.build(M);
        analysis::LLVMPointsToAnalysis PTA(&d);

        tm.start();
        PTA.run();
        tm.stop();
        tm.report("INFO: Points-to analysis [old] took");
    }

    std::set<LLVMNode *> callsites;
    if (slicing_criterion) {
        const char *sc[] = {
            slicing_criterion,
            "klee_assume",
            NULL
        };

        tm.start();
        d.getCallSites(sc, &callsites);
        tm.stop();
        tm.report("INFO: Finding slicing criterions took");
    }

    if (strcmp(pts, "old")) {
        assert(PTA && "BUG: Need points-to analysis");
        //use new analyses
        analysis::rd::LLVMReachingDefinitions RDA(M, PTA);
        tm.start();
        RDA.run();  // compute reaching definitions
        tm.stop();
        tm.report("INFO: Reaching defs analysis took");

        LLVMDefUseAnalysis DUA(&d, &RDA, PTA);
        tm.start();
        DUA.run(); // add def-use edges according that
        tm.stop();
        tm.report("INFO: Adding Def-Use edges took");

        // we won't need PTA anymore
        delete PTA;
    } else {
        analysis::LLVMReachingDefsAnalysis RDA(&d);
        tm.start();
        RDA.run();  // compute reaching definitions
        tm.stop();
        tm.report("INFO: Reaching defs analysis [old] took");

        analysis::old::LLVMDefUseAnalysis DUA(&d);
        tm.start();
        DUA.run(); // add def-use edges according that
        tm.stop();
        tm.report("INFO: Adding Def-Use edges [old] took");
    }

    tm.start();
    // add post-dominator frontiers
    d.computeControlDependencies(cd_alg);
    tm.stop();
    tm.report("INFO: computing control dependencies took");

    if (slicing_criterion) {
        LLVMSlicer slicer;
        tm.start();

        if (strcmp(slicing_criterion, "ret") == 0) {
            if (mark_only)
                slicer.mark(d.getExit());
            else
                slicer.slice(&d, d.getExit());
        } else {
            if (callsites.empty()) {
                errs() << "ERR: slicing criterion not found: "
                       << slicing_criterion << "\n";
                exit(1);
            }

            uint32_t slid = 0;
            for (LLVMNode *start : callsites)
                slid = slicer.mark(start, slid);

            if (!mark_only)
               slicer.slice(&d, nullptr, slid);
        }

        // there's overhead but nevermind
        tm.stop();
        tm.report("INFO: Slicing took");

        if (!mark_only) {
            std::string fl(module);
            fl.append(".sliced");
            std::ofstream ofs(fl);
            llvm::raw_os_ostream output(ofs);

            analysis::SlicerStatistics& st = slicer.getStatistics();
            errs() << "INFO: Sliced away " << st.nodesRemoved
                   << " from " << st.nodesTotal << " nodes\n";

            llvm::WriteBitcodeToFile(M, output);
        }
    }

    if (bb_only) {
        LLVMDGDumpBlocks dumper(&d, opts);
        dumper.dump(nullptr, dump_func_only);
    } else {
        LLVMDG2Dot dumper(&d, opts);
        dumper.dump(nullptr, dump_func_only);
    }

    return 0;
}
