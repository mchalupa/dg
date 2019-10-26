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

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
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

#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"
#include "dg/PointerAnalysis/Pointer.h"

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/DataDependence/DataDependence.h"

#include "dg/util/debug.h"
#include "TimeMeasure.h"

using namespace dg;
using namespace dg::dda;
using llvm::errs;

static bool verbose = false;
static const char *entryFunc = "main";

static bool
compareDefs(llvm::Instruction& I, LLVMDataDependenceAnalysis *ssa, LLVMDataDependenceAnalysis *rd) {
    bool stat = true;

    auto ssaD = ssa->getLLVMDefinitions(&I);
    auto rdD = rd->getLLVMDefinitions(&I);

    // not very efficient, but what the hell...
    for (auto& ssadef : ssaD) {
        if (std::find(rdD.begin(), rdD.end(), ssadef) == rdD.end()) {
            llvm::errs() << "SSA has but RD does not:\n" << *ssadef << "\n";
            stat = false;
        }
    }

    for (auto& rddef : rdD) {
        if (std::find(ssaD.begin(), ssaD.end(), rddef) == ssaD.end()) {
            llvm::errs() << "RD has but SSA does not:\n" << *rddef << "\n";
            stat = false;
        }
    }

    return stat;
}

static bool
compareDefs(llvm::Module *M, LLVMDataDependenceAnalysis *ssa, LLVMDataDependenceAnalysis *rd) {
    bool stat = false;
    for (auto& F : *M) {
        for (auto& B : F) {
            for (auto& I : B) {
                if (I.mayReadFromMemory()) {
                    stat |= !compareDefs(I, ssa, rd);
                }
            }
        }
    }

    return !stat;
}

int main(int argc, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    bool threads = false;
    const char *module = nullptr;
    Offset::type field_sensitivity = Offset::UNKNOWN;
    bool rd_strong_update_unknown = false;
    Offset::type max_set_size = Offset::UNKNOWN;

    enum {
        FLOW_SENSITIVE = 1,
        FLOW_INSENSITIVE,
    } type = FLOW_INSENSITIVE;

    enum class RdaType {
        RD,
        SSA,
        BOTH
    } rda = RdaType::BOTH;

    // parse options
    for (int i = 1; i < argc; ++i) {
        // run given points-to analysis
        if (strcmp(argv[i], "-pta") == 0) {
            if (strcmp(argv[i+1], "fs") == 0)
                type = FLOW_SENSITIVE;
        } else if (strcmp(argv[i], "-dda") == 0) {
            if (strcmp(argv[i+1], "ssa") == 0)
                rda = RdaType::SSA;
            if (strcmp(argv[i+1], "rd") == 0)
                rda = RdaType::RD;
        } else if (strcmp(argv[i], "-pta-field-sensitive") == 0) {
            field_sensitivity = static_cast<Offset::type>(atoll(argv[i + 1]));
        } else if (strcmp(argv[i], "-rd-max-set-size") == 0) {
            max_set_size = static_cast<Offset::type>(atoll(argv[i + 1]));
            if (max_set_size == 0) {
                llvm::errs() << "Invalid -rd-max-set-size argument\n";
                abort();
            }
        } else if (strcmp(argv[i], "-rd-strong-update-unknown") == 0) {
            rd_strong_update_unknown = true;
        } else if (strcmp(argv[i], "-threads") == 0) {
            threads = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-dbg") == 0) {
            DBG_ENABLE();
        } else if (strcmp(argv[i], "-entry") == 0) {
            entryFunc = argv[i+1];
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module [-pts fs|fi] [-dot] [-v] [output_file]\n";
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

    LLVMPointerAnalysisOptions ptaopts;
    ptaopts.setEntryFunction(entryFunc);
    ptaopts.setFieldSensitivity(field_sensitivity);
    ptaopts.threads = threads;

    if (type == FLOW_INSENSITIVE) {
        ptaopts.analysisType = LLVMPointerAnalysisOptions::AnalysisType::fi;
    } else {
        ptaopts.analysisType = LLVMPointerAnalysisOptions::AnalysisType::fs;
    }

    DGLLVMPointerAnalysis PTA(M, ptaopts);

    tm.start();
    PTA.run();

    tm.stop();
    tm.report("INFO: Pointer analysis took");

    LLVMDataDependenceAnalysisOptions optsbase;
    optsbase.threads = threads;
    optsbase.entryFunction = entryFunc;
    optsbase.strongUpdateUnknown = rd_strong_update_unknown;
    optsbase.maxSetSize = max_set_size;

    std::unique_ptr<LLVMDataDependenceAnalysis> ssa;
    std::unique_ptr<LLVMDataDependenceAnalysis> rd;

    if (rda == RdaType::SSA) {
        LLVMDataDependenceAnalysisOptions opts = optsbase;
        opts.analysisType = DataDependenceAnalysisOptions::AnalysisType::ssa;
        ssa = std::unique_ptr<LLVMDataDependenceAnalysis>(new LLVMDataDependenceAnalysis(M, &PTA, opts));
    } else if (rda == RdaType::RD) {
        LLVMDataDependenceAnalysisOptions opts = optsbase;
        opts.analysisType = DataDependenceAnalysisOptions::AnalysisType::rd;
        rd = std::unique_ptr<LLVMDataDependenceAnalysis>(new LLVMDataDependenceAnalysis(M, &PTA, opts));
    } else {
        LLVMDataDependenceAnalysisOptions opts = optsbase;
        opts.analysisType = DataDependenceAnalysisOptions::AnalysisType::ssa;
        ssa = std::unique_ptr<LLVMDataDependenceAnalysis>(new LLVMDataDependenceAnalysis(M, &PTA, opts));
        opts.analysisType = DataDependenceAnalysisOptions::AnalysisType::rd;
        rd = std::unique_ptr<LLVMDataDependenceAnalysis>(new LLVMDataDependenceAnalysis(M, &PTA, opts));
    }

    if (ssa) {
        tm.start();
        ssa->run();
        tm.stop();
        tm.report("INFO: Memory SSA DDA took");
    }

    if (rd) {
        tm.start();
        rd->run();
        tm.stop();
        tm.report("INFO: Reaching definitions DDA took");
    }

    if (ssa && rd) {
        return compareDefs(M, ssa.get(), rd.get());
    }

    return 0;
}
