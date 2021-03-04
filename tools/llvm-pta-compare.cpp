#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <set>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/PrettyStackTrace.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif
SILENCE_LLVM_WARNINGS_POP

#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "dg/tools/llvm-slicer-opts.h"

using namespace dg;
using namespace dg::pta;
using llvm::errs;

llvm::cl::opt<bool> enable_debug("dbg",
    llvm::cl::desc("Enable debugging messages (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> uoff_covers("uoff-covers",
    llvm::cl::desc("Pointers with unknown offset cover pointers with concrete"
                   "offsets.(default=true)."),
    llvm::cl::init(true), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> unknown_covers("unknown-covers",
    llvm::cl::desc("Unknown pointers cover all concrete pointers (default=true)."),
    llvm::cl::init(true), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> strict("strict",
    llvm::cl::desc("Compare points-to sets by element by element."
                   "I.e., uoff-covers=false and unknown-covers=false (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> fi("fi",
    llvm::cl::desc("Run flow-insensitive PTA."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> fs("fs",
    llvm::cl::desc("Run flow-sensitive PTA."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> fsinv("fsinv",
    llvm::cl::desc("Run flow-sensitive PTA with invalidated memory analysis."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

#if HAVE_SVF
llvm::cl::opt<bool> svf("svf",
    llvm::cl::desc("Run SVF PTA (Andersen)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));
#endif


static std::string valToStr(const llvm::Value *val) {
    using namespace llvm;

    std::ostringstream ostr;
    raw_os_ostream ro(ostr);

    if (auto *F = dyn_cast<Function>(val)) {
        ro << "fun '" << F->getName().str() << "'";
    } else {
        if (auto *I = dyn_cast<Instruction>(val)) {
            ro << I->getParent()->getParent()->getName().str();
            ro << "::";
        }

        assert(val);
        ro << *val;
    }

    ro.flush();

    return ostr.str();
}

static std::string offToStr(const Offset& off) {
    if (off.isUnknown())
        return "?";
    return std::to_string(*off);
}

static bool verify_ptsets(const llvm::Value *val,
                          const std::string& N1,
                          const std::string& N2,
                          LLVMPointerAnalysis *A1,
                          LLVMPointerAnalysis *A2) {
    bool ret = true;

    auto ptset1 = A1->getLLVMPointsTo(val);
    auto ptset2 = A2->getLLVMPointsTo(val);

   //llvm::errs() << "Points-to for: " << *val << "\n";
   //for (const auto& ptr : ptset1) {
   //    llvm::errs() << "  " << N1 << *ptr.value << "\n";
   //}
   //if (ptset1.hasUnknown()) {
   //    llvm::errs() << N1 << "  unknown\n";
   //}

   //for (const auto& ptr : ptset2) {
   //    llvm::errs() << "  " << N2 << *ptr.value << "\n";
   //}
   //if (ptset2.hasUnknown()) {
   //    llvm::errs() << N2 << "  unknown\n";
   //}

    for (const auto& ptr : ptset1) {
        bool found = false;
        if (unknown_covers && ptset2.hasUnknown()) {
            found = true;
        } else {
            for (const auto& ptr2 : ptset2) {
                if (ptr == ptr2) {
                    found = true;
                    break;
                } else if (uoff_covers &&
                           ptr.value == ptr2.value &&
                           ptr2.offset.isUnknown()) {
                        found = true;
                        break;
                }
            }
        }

        if (!found) {
                llvm::errs() << N1 << " has a pointer that " << N2
                             << " does not:\n";
                llvm::errs() << "  " << valToStr(val)
                             << " -> " << valToStr(ptr.value)
                             << " + " << offToStr(ptr.offset) << "\n";
                ret = false;
        }
    }

    return ret;
}

static bool verify_ptsets(llvm::Module *M,
                          const std::string& N1,
                          const std::string& N2,
                          LLVMPointerAnalysis *A1,
                          LLVMPointerAnalysis *A2) {
    using namespace llvm;

    if (A1 == A2)
        return true;

    bool ret = true;

    for (Function& F : *M) {
        for (BasicBlock& B : F) {
            for (Instruction& I : B) {
                if (!verify_ptsets(&I, N1, N2, A1, A2))
                    ret = false;
            }
        }
    }

    return ret;
}

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
        SMD.print("llvm-pta-compare", llvm::errs());
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

template <typename PTAObj>
std::unique_ptr<LLVMPointerAnalysis>
createAnalysis(llvm::Module *M, const LLVMPointerAnalysisOptions& opts) {
    return std::unique_ptr<LLVMPointerAnalysis>(new PTAObj(M, opts));
}

int main(int argc, char *argv[])
{
    setupStackTraceOnError(argc, argv);

    SlicerOptions options = parseSlicerOptions(argc, argv,
                                               /* requireCrit = */ false);

    if (enable_debug) {
        DBG_ENABLE();
    }

    if (strict) {
        uoff_covers = false;
        unknown_covers = false;
    }

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> M = parseModule(context, options);
    if (!M) {
        llvm::errs() << "Failed parsing '" << options.inputFile << "' file:\n";
        return 1;
    }

    std::vector<std::tuple<std::string,
                           std::unique_ptr<LLVMPointerAnalysis>,
                           size_t>> analyses;

    clock_t start, end, elapsed;
    auto& opts = options.dgOptions.PTAOptions;

    if (fi) {
        opts.analysisType = dg::LLVMPointerAnalysisOptions::AnalysisType::fi;
        analyses.emplace_back("DG FI",
                              createAnalysis<DGLLVMPointerAnalysis>(M.get(), opts), 0);
    }
    if (fs) {
        opts.analysisType = dg::LLVMPointerAnalysisOptions::AnalysisType::fs;
        analyses.emplace_back("DG FS",
                              createAnalysis<DGLLVMPointerAnalysis>(M.get(), opts), 0);
    }
    if (fsinv) {
        opts.analysisType = dg::LLVMPointerAnalysisOptions::AnalysisType::inv;
        analyses.emplace_back("DG FSinv",
                              createAnalysis<DGLLVMPointerAnalysis>(M.get(), opts), 0);
    }
#ifdef HAVE_SVF
    if (svf) {
        opts.analysisType = dg::LLVMPointerAnalysisOptions::AnalysisType::svf;
        analyses.emplace_back("SVF (Andersen)",
                              createAnalysis<SVFPointerAnalysis>(M.get(), opts), 0);
    }
#endif

    for (auto& it : analyses) {
        start = clock();
        std::get<1>(it)->run(); // compute all the information
        end = clock();
        elapsed = end - start;
        std::get<2>(it) += elapsed;
        std::cout << "  " << std::get<0>(it) << ": "
                  << static_cast<float>(elapsed) / CLOCKS_PER_SEC << " s ("
                  << elapsed << " ticks)\n";
        std::cout << "-----" << std::endl;
    }

    if (analyses.size() < 2)
        return 0;

    int ret = 0;
    for (auto& analysis1 : analyses) {
        for (auto& analysis2 : analyses) {
            ret = !verify_ptsets(M.get(),
                                 std::get<0>(analysis1),
                                 std::get<0>(analysis2),
                                 std::get<1>(analysis1).get(),
                                 std::get<1>(analysis2).get());
        }
    }

    return ret;
}
