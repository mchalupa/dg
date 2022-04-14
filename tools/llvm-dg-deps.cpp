#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

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

#include "dg/PointerAnalysis/Pointer.h"
#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/PointerAnalysis/PointerAnalysisFS.h"

#include "dg/llvm/DataDependence/DataDependence.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "dg/tools/llvm-slicer-opts.h"
#include "dg/tools/llvm-slicer-utils.h"

#include "dg/util/TimeMeasure.h"
#include "dg/util/debug.h"

using namespace dg;
using namespace dg::dda;
using llvm::errs;

llvm::cl::opt<bool> enable_debug(
        "dbg", llvm::cl::desc("Enable debugging messages (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

/*
llvm::cl::opt<bool> verbose("v",
                            llvm::cl::desc("Verbose output (default=false)."),
                            llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> quiet("q", llvm::cl::desc("No output (for benchmarking)."),
                          llvm::cl::init(false), llvm::cl::cat(SlicingOpts));
*/

llvm::cl::opt<std::string> ctrl("ctrl",
                                llvm::cl::desc("Dump control dependencies of"),
                                llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<std::string> data("data",
                                llvm::cl::desc("Dump data dependencies of"),
                                llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dump_c_lines(
        "c-lines",
        llvm::cl::desc("Dump output as C lines (line:column where possible)."
                       "Requires metadata in the bitcode (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

using VariablesMapTy = std::map<const llvm::Value *, CVariableDecl>;
VariablesMapTy allocasToVars(const llvm::Module &M);
VariablesMapTy valuesToVars;

static inline size_t count_ws(const std::string &str) {
    size_t n = 0;
    while (isspace(str[n])) {
        ++n;
    }
    return n;
}

static inline size_t trim_name_idx(const std::string &str) {
    // skip, e.g., align attributes, etc.
    auto m = str.rfind(", align");
    if (m == std::string::npos)
        return str.length();
    return m - 1;
}

static std::string getInstName(const llvm::Value *val) {
    assert(val);
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    if (dump_c_lines) {
        if (const auto *I = llvm::dyn_cast<llvm::Instruction>(val)) {
            const auto &DL = I->getDebugLoc();
            if (DL) {
                ro << DL.getLine() << ":" << DL.getCol();
            } else {
                auto Vit = valuesToVars.find(I);
                if (Vit != valuesToVars.end()) {
                    auto &decl = Vit->second;
                    ro << decl.line << ":" << decl.col;
                } else {
                    ro << "(no dbg) ";
                    ro << *val;
                }
            }
        }
    } else {
        ro << *val;
    }

    ro.flush();

    auto str = ostr.str();
    auto n = count_ws(str);
    auto m = trim_name_idx(str);
    if (n > 0)
        str = str.substr(n, m);

    if (dump_c_lines)
        return str;

    if (const auto *I = llvm::dyn_cast<llvm::Instruction>(val)) {
        const auto &fun = I->getParent()->getParent()->getName();
        auto funstr = fun.str();
        if (funstr.length() > 15)
            funstr = funstr.substr(0, 15);
        funstr += "::";
        funstr += str;
        return funstr;
    }

    return str;
}

int main(int argc, char *argv[]) {
    setupStackTraceOnError(argc, argv);
    SlicerOptions options = parseSlicerOptions(argc, argv);

    if (enable_debug) {
        DBG_ENABLE();
    }

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> M =
            parseModule("llvm-dg-deps", context, options);
    if (!M)
        return 1;

    if (!M->getFunction(options.dgOptions.entryFunction)) {
        llvm::errs() << "The entry function not found: "
                     << options.dgOptions.entryFunction << "\n";
        return 1;
    }

    if (dump_c_lines) {
#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
        llvm::errs() << "WARNING: Variables names matching is not supported "
                        "for LLVM older than 3.7\n";
#else
        valuesToVars = allocasToVars(*M);
#endif // LLVM > 3.6
        if (valuesToVars.empty()) {
            llvm::errs() << "WARNING: No debugging information found, "
                         << "the C lines output will be corrupted\n";
        }
    }

    debug::TimeMeasure tm;

    DGLLVMPointerAnalysis PTA(M.get(), options.dgOptions.PTAOptions);

    tm.start();
    PTA.run();

    tm.stop();
    tm.report("INFO: Pointer analysis took");

    if (!ctrl.empty()) {
        auto values = getSlicingCriteriaValues(*M.get(), ctrl, "", "", false);
        if (values.empty()) {
            llvm::errs() << "No instruction found for '" << ctrl << "'\n";
        } else {
            tm.start();
            LLVMControlDependenceAnalysis CDA(M.get(),
                                              options.dgOptions.CDAOptions);
            tm.stop();
            tm.report("INFO: Control dependence analysis init took");
            for (auto *val : values) {
                const auto *I = llvm::dyn_cast<llvm::Instruction>(val);
                if (!I)
                    continue;
                llvm::errs() << getInstName(val) << "\n";
                for (auto *dep : CDA.getDependencies(I)) {
                    llvm::errs() << "   CD -> " << getInstName(dep) << "\n";
                }
                for (auto *dep : CDA.getDependencies(I->getParent())) {
                    llvm::errs() << "   CD -> "
                                 << *(llvm::cast<llvm::BasicBlock>(dep)
                                              ->getTerminator())
                                 << "\n";
                }
            }
        }
    }

    if (!data.empty()) {
        auto values = getSlicingCriteriaValues(*M.get(), data, "", "", false);
        if (values.empty()) {
            llvm::errs() << "No instruction found for '" << ctrl << "'\n";
        } else {
            tm.start();
            LLVMDataDependenceAnalysis DDA(M.get(), &PTA,
                                           options.dgOptions.DDAOptions);
            DDA.run();
            tm.stop();
            tm.report("INFO: Data dependence analysis took");

            for (auto *val : values) {
                if (!DDA.isUse(val))
                    continue;

                llvm::errs() << getInstName(val) << "\n";
                auto defs =
                        DDA.getLLVMDefinitions(const_cast<llvm::Value *>(val));
                if (defs.empty()) {
                    llvm::errs() << "   DD -> none\n";
                    continue;
                }
                for (auto *dep : defs) {
                    llvm::errs() << "   DD -> " << getInstName(dep) << "\n";
                }
            }
        }
    }

    return 0;
}
