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
#include "llvm/LLVMSlicer.h"
#include "llvm/LLVMDG2Dot.h"
#include "TimeMeasure.h"

#include "llvm/analysis/DefUse/DefUse.h"
#include "llvm/analysis/PointsTo/PointerAnalysis.h"
#include "llvm/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "analysis/PointsTo/PointsToFlowSensitive.h"
#include "analysis/PointsTo/PointsToFlowInsensitive.h"
#include "analysis/PointsTo/PointsToWithInvalidate.h"

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
    const char *rda = "dense";
    const char *entry_func = "main";
    CD_ALG cd_alg = CD_ALG::CLASSIC;

    using namespace debug;
    uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD | PRINT_USE;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-no-control") == 0) {
            opts &= ~PRINT_CD;
        } else if (strcmp(argv[i], "-no-use") == 0) {
            opts &= ~PRINT_USE;
        } else if (strcmp(argv[i], "-pta") == 0) {
            pts = argv[++i];
        } else if (strcmp(argv[i], "-rda") == 0) {
            rda = argv[++i];
        } else if (strcmp(argv[i], "-no-data") == 0) {
            opts &= ~PRINT_DD;
        } else if (strcmp(argv[i], "-no-cfg") == 0) {
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
        } else if (strcmp(argv[i], "-entry") == 0) {
            entry_func = argv[++i];
        } else if (strcmp(argv[i], "-cd-alg") == 0) {
            const char *arg = argv[++i];
            if (strcmp(arg, "classic") == 0)
                cd_alg = CD_ALG::CLASSIC;
            else if (strcmp(arg, "ce") == 0)
                cd_alg = CD_ALG::CONTROL_EXPRESSION;
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
    analysis::LLVMPointerAnalysisOptions ptaOpts;
    ptaOpts.entryFunction = entry_func;
    LLVMPointerAnalysis *PTA = new LLVMPointerAnalysis(M, ptaOpts);

    if (strcmp(pts, "fs") == 0) {
        tm.start();
        PTA->run<analysis::pta::PointsToFlowSensitive>();
        tm.stop();
    } else if (strcmp(pts, "fi") == 0) {
        tm.start();
        PTA->run<analysis::pta::PointsToFlowInsensitive>();
        tm.stop();
    } else if (strcmp(pts, "inv") == 0) {
        tm.start();
        PTA->run<analysis::pta::PointsToWithInvalidate>();
        tm.stop();
    } else {
        llvm::errs() << "Unknown points to analysis, try: fs, fi\n";
        abort();
    }

    tm.report("INFO: Points-to analysis took");

    assert(PTA && "BUG: Need points-to analysis");
    //use new analyses
    analysis::LLVMReachingDefinitionsAnalysisOptions rdOpts;
    rdOpts.entryFunction = entry_func;

    analysis::rd::LLVMReachingDefinitions RDA(M, PTA, rdOpts);
    tm.start();

    if (strcmp(rda, "dense") == 0) {
        tm.start();
        RDA.run<analysis::rd::ReachingDefinitionsAnalysis>();
        tm.stop();
    } else if (strcmp(rda, "ss") == 0) {
        tm.start();
        RDA.run<analysis::rd::SemisparseRda>();
        tm.stop();
    } else {
        llvm::errs() << "Unknown reaching definitions analysis, try: dense, ss\n";
        abort();
    }
    tm.stop();
    tm.report("INFO: Reaching defs analysis took");

    LLVMDefUseAnalysis DUA(&d, &RDA, PTA);
    tm.start();
    DUA.run(); // add def-use edges according that
    tm.stop();
    tm.report("INFO: Adding Def-Use edges took");

    // we won't need PTA anymore
    delete PTA;

    tm.start();
    // add post-dominator frontiers
    d.computeControlDependencies(cd_alg);
    tm.stop();
    tm.report("INFO: computing control dependencies took");

    d.build(M, PTA, &RDA, M->getFunction(entry_func));

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

#if (LLVM_VERSION_MAJOR > 6)
            llvm::WriteBitcodeToFile(*M, output);
#else
            llvm::WriteBitcodeToFile(M, output);
#endif
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
