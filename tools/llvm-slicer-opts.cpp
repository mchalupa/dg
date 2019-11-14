#include "dg/Offset.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMDependenceGraphBuilder.h"
#include "dg/llvm/PointerAnalysis/LLVMPointerAnalysisOptions.h"
#include "dg/llvm/DataDependence/LLVMDataDependenceAnalysisOptions.h"

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Support/CommandLine.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "llvm-slicer.h"
#include "llvm-slicer-utils.h"

#include "git-version.h"

using dg::LLVMPointerAnalysisOptions;
using dg::LLVMDataDependenceAnalysisOptions;

static void
addAllocationFuns(dg::llvmdg::LLVMDependenceGraphOptions& dgOptions,
                  const std::string& allocationFuns) {
    using dg::AllocationFunction;

    auto items = splitList(allocationFuns);
    for (auto& item : items) {
        auto subitms = splitList(item, ':');
        if (subitms.size() != 2) {
            llvm::errs() << "ERROR: Invalid allocation function: " << item << "\n";
            continue;
        }

        AllocationFunction type;
        if (subitms[1] == "malloc")
            type = AllocationFunction::MALLOC;
        else if (subitms[1] == "calloc")
            type = AllocationFunction::CALLOC;
        else if (subitms[1] == "realloc")
            type = AllocationFunction::REALLOC;
        else {
            llvm::errs() << "ERROR: Invalid type of allocation function: "
                         << item << "\n";
            continue;
        }

        dgOptions.PTAOptions.addAllocationFunction(subitms[0], type);
        dgOptions.DDAOptions.addAllocationFunction(subitms[0], type);
    }
}


llvm::cl::OptionCategory SlicingOpts("Slicer options", "");

// Use LLVM's CommandLine library to parse
// command line arguments
SlicerOptions parseSlicerOptions(int argc, char *argv[]) {
    llvm::cl::opt<std::string> outputFile("o",
        llvm::cl::desc("Save the output to given file. If not specified,\n"
                       "a .sliced suffix is used with the original module name."),
        llvm::cl::value_desc("filename"), llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<std::string> inputFile(llvm::cl::Positional, llvm::cl::Required,
        llvm::cl::desc("<input file>"), llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<std::string> slicingCriteria("c", llvm::cl::Required,
        llvm::cl::desc("Slice with respect to the call-sites of a given function\n"
                       "i. e.: '-c foo' or '-c __assert_fail'. Special value is a 'ret'\n"
                       "in which case the slice is taken with respect to the return value\n"
                       "of the main function. Further, you can specify the criterion as\n"
                       "l:v where l is the line in the original code and v is the variable.\n"
                       "l must be empty when v is a global variable. For local variables,\n"
                       "the variable v must be used on the line l.\n"
                       "You can use comma-separated list of more slicing criteria,\n"
                       "e.g. -c foo,5:x,:glob\n"), llvm::cl::value_desc("crit"),
                       llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<std::string> secondarySlicingCriteria("2c",
        llvm::cl::desc("Set secondary slicing criterion. The criterion is a call\n"
                       "to a given function. If just a name of the function is\n"
                       "given, it is a 'control' slicing criterion. If there is ()\n"
                       "appended, it is 'data' slicing criterion. E.g. foo means\n"
                       "control secondary slicing criterion, foo() means data\n"
                       "data secondary slicing criterion.\n"),
                       llvm::cl::value_desc("crit"),
                       llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> removeSlicingCriteria("remove-slicing-criteria",
        llvm::cl::desc("By default, slicer keeps also calls to the slicing criteria\n"
                       "in the sliced program. This switch makes slicer to remove\n"
                       "also the calls (i.e. behave like Weisser's algorithm)"),
                       llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<std::string> preservedFuns("preserved-functions",
        llvm::cl::desc("Do not slice bodies of the given functions\n."
                       "The argument is a comma-separated list of functions.\n"),
                       llvm::cl::value_desc("funs"),
                       llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> terminationSensitive("termination-sensitive",
        llvm::cl::desc("Do not slice away parts of programs that might make\n"
                       "the slicing criteria unreachable (e.g. calls to exit() or potentially infinite loops).\n"
                       "NOTE: at this moment we do not handle potentially infinite loops.\n"
                       "Default: on\n"),
                       llvm::cl::init(true), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<uint64_t> ptaFieldSensitivity("pta-field-sensitive",
        llvm::cl::desc("Make PTA field sensitive/insensitive. The offset in a pointer\n"
                       "is cropped to Offset::UNKNOWN when it is greater than N bytes.\n"
                       "Default is full field-sensitivity (N = Offset::UNKNOWN).\n"),
                       llvm::cl::value_desc("N"), llvm::cl::init(dg::Offset::UNKNOWN),
                       llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> rdaStrongUpdateUnknown("rd-strong-update-unknown",
        llvm::cl::desc("Let reaching defintions analysis do strong updates on memory defined\n"
                       "with uknown offset in the case, that new definition overwrites\n"
                       "the whole memory. May be unsound for out-of-bound access\n"),
                       llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> undefinedArePure("undefined-are-pure",
        llvm::cl::desc("Assume that undefined functions have no side-effects\n"),
                       llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<std::string> entryFunction("entry",
        llvm::cl::desc("Entry function of the program\n"),
                       llvm::cl::init("main"), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> forwardSlicing("forward",
        llvm::cl::desc("Perform forward slicing\n"),
                       llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> threads("threads",
        llvm::cl::desc("Consider threads are in input file (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<std::string> allocationFuns("allocation-funs",
        llvm::cl::desc("Treat these functions as allocation functions\n"
                       "The argument is a comma-separated list of func:type,\n"
                       "where func is the function and type is one of\n"
                       "malloc, calloc, or realloc.\n"
                       "E.g., myAlloc:malloc will treat myAlloc as malloc.\n"),
                       llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<LLVMPointerAnalysisOptions::AnalysisType> ptaType("pta",
        llvm::cl::desc("Choose pointer analysis to use:"),
        llvm::cl::values(
            clEnumValN(LLVMPointerAnalysisOptions::AnalysisType::fi, "fi", "Flow-insensitive PTA (default)"),
            clEnumValN(LLVMPointerAnalysisOptions::AnalysisType::fs, "fs", "Flow-sensitive PTA"),
            clEnumValN(LLVMPointerAnalysisOptions::AnalysisType::inv, "inv", "PTA with invalidate nodes")
#ifdef HAVE_SVF
            , clEnumValN(LLVMPointerAnalysisOptions::AnalysisType::svf, "svf", "Use pointer analysis from SVF project")
#endif
    #if LLVM_VERSION_MAJOR < 4
            , nullptr
    #endif
            ),
        llvm::cl::init(LLVMPointerAnalysisOptions::AnalysisType::fi), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<LLVMDataDependenceAnalysisOptions::AnalysisType> ddaType("dda",
        llvm::cl::desc("Choose reaching definitions analysis to use:"),
        llvm::cl::values(
            clEnumValN(LLVMDataDependenceAnalysisOptions::AnalysisType::rd,
                       "rd", "Reaching definitions DDA"),
            clEnumValN(LLVMDataDependenceAnalysisOptions::AnalysisType::ssa,
                       "ssa", "MemorySSA DDA")
    #if LLVM_VERSION_MAJOR < 4
            , nullptr
    #endif
            ),
        llvm::cl::init(LLVMDataDependenceAnalysisOptions::AnalysisType::ssa),
                       llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<dg::CD_ALG> cdAlgorithm("cd-alg",
        llvm::cl::desc("Choose control dependencies algorithm to use:"),
        llvm::cl::values(
            clEnumValN(dg::CD_ALG::CLASSIC , "classic", "Ferrante's algorithm (default)"),
            clEnumValN(dg::CD_ALG::CONTROL_EXPRESSION, "ce", "Control expression based (experimental)"),
            clEnumValN(dg::CD_ALG::NTSCD, "ntscd", "Non-termination sensitive control dependencies algorithm")
    #if LLVM_VERSION_MAJOR < 4
            , nullptr
    #endif
             ),
        llvm::cl::init(dg::CD_ALG::CLASSIC), llvm::cl::cat(SlicingOpts));

    ////////////////////////////////////
    // ===-- End of the options --=== //
    ////////////////////////////////////

    // hide all options except ours options
    // this method is available since LLVM 3.7
#if ((LLVM_VERSION_MAJOR > 3)\
      || ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR >= 7)))
    llvm::cl::HideUnrelatedOptions(SlicingOpts);
#endif
# if ((LLVM_VERSION_MAJOR >= 6))
    llvm::cl::SetVersionPrinter([](llvm::raw_ostream&){ printf("%s\n", GIT_VERSION); });
#else
    llvm::cl::SetVersionPrinter([](){ printf("%s\n", GIT_VERSION); });
#endif
    llvm::cl::ParseCommandLineOptions(argc, argv);

    /// Fill the structure
    SlicerOptions options;

    options.inputFile = inputFile;
    options.outputFile = outputFile;
    options.slicingCriteria = slicingCriteria;
    options.secondarySlicingCriteria = secondarySlicingCriteria;
    options.preservedFunctions = splitList(preservedFuns);
    options.removeSlicingCriteria = removeSlicingCriteria;
    options.forwardSlicing = forwardSlicing;

    options.dgOptions.entryFunction = entryFunction;
    options.dgOptions.PTAOptions.entryFunction = entryFunction;
    options.dgOptions.PTAOptions.fieldSensitivity
                                    = dg::Offset(ptaFieldSensitivity);
    options.dgOptions.PTAOptions.analysisType = ptaType;

    options.dgOptions.threads = threads;
    options.dgOptions.PTAOptions.threads = threads;
    options.dgOptions.DDAOptions.threads = threads;

    options.dgOptions.DDAOptions.entryFunction = entryFunction;
    options.dgOptions.DDAOptions.strongUpdateUnknown = rdaStrongUpdateUnknown;
    options.dgOptions.DDAOptions.undefinedArePure = undefinedArePure;
    options.dgOptions.DDAOptions.analysisType = ddaType;

    addAllocationFuns(options.dgOptions, allocationFuns);

    // FIXME: add options class for CD
    options.dgOptions.cdAlgorithm = cdAlgorithm;
    options.dgOptions.terminationSensitive = terminationSensitive;

    return options;
}

