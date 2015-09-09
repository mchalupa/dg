#include <assert.h>
#include <cstdio>

#include <set>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Bitcode/ReaderWriter.h>

#include <iostream>
#include <fstream>
#include "llvm/LLVMDependenceGraph.h"
#include "llvm/PointsTo.h"
#include "llvm/DefUse.h"
#include "llvm/Slicer.h"
#include "Utils.h"

using namespace dg;
using llvm::errs;

static bool slice(llvm::Module *M, const char *slicing_criterion)
{
    debug::TimeMeasure tm;
    LLVMDependenceGraph d;
    std::set<LLVMNode *> gatheredCallsites;

    d.gatherCallsites(slicing_criterion, &gatheredCallsites);

    // build the graph
    d.build(&*M);

    if (gatheredCallsites.empty()) {
        if (strcmp(slicing_criterion, "ret") == 0)
            gatheredCallsites.insert(d.getExit());
        else {
            errs() << "Did not find slicing criterion: " << slicing_criterion << "\n";
            return false;
        }
    }

    analysis::LLVMPointsToAnalysis PTA(&d);

    tm.start();
    PTA.run();
    tm.stop();
    tm.report("INFO: Points-to analysis took");

    analysis::LLVMDefUseAnalysis DUA(&d);

    tm.start();
    DUA.run();  // compute reaching definitions
    tm.stop();
    tm.report("INFO: Reaching defs analysis took");

    tm.start();
    DUA.addDefUseEdges(); // add def-use edges according that
    tm.stop();
    tm.report("INFO: Adding Def-Use edges took");

    LLVMSlicer slicer;
    uint32_t slid = 0;

    tm.start();
    for (LLVMNode *start : gatheredCallsites)
        slid = slicer.mark(start, slid);

    slicer.slice(&d, nullptr, slid);

    tm.stop();
    tm.report("INFO: Slicing took");

    auto st = slicer.getStatistics();
    errs() << "INFO: Sliced away " << st.second
           << " from " << st.first << " nodes\n";

    return true;
}

static bool write_module(llvm::Module *M, const char *module_name)
{
    // compose name
    std::string fl(module_name);
    fl.replace(fl.end() - 3, fl.end(), ".sliced");

    // open stream to write to
    std::ofstream ofs(fl);
    llvm::raw_os_ostream output(ofs);

    // write the module
    errs() << "INFO: saving sliced module to: " << fl.c_str() << "\n";
    llvm::WriteBitcodeToFile(M, output);

    return true;
}

int main(int argc, char *argv[])
{
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    std::unique_ptr<llvm::Module> M;

    const char *slicing_criterion = NULL;
    const char *module = NULL;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-c") == 0
            || strcmp(argv[i], "-crit") == 0
            || strcmp(argv[i], "-slice") == 0){
            slicing_criterion = argv[++i];
        } else {
            module = argv[i];
        }
    }

    if (!slicing_criterion || !module) {
        errs() << "Usage: % [-c|-crit|-slice] func_call module\n";
        return 1;
    }

    M = llvm::parseIRFile(module, SMD, context);
    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    if (!slice(&*M, slicing_criterion)) {
        errs() << "ERR: Slicing failed\n";
        return 1;
    }

    if (!write_module(&*M, module)) {
        errs() << "Saving sliced module failed\n";
        return 1;
    }

    return 0;
}
