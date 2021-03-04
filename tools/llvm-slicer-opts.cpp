#include "dg/Offset.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMDependenceGraphBuilder.h"
#include "dg/llvm/PointerAnalysis/LLVMPointerAnalysisOptions.h"
#include "dg/llvm/DataDependence/LLVMDataDependenceAnalysisOptions.h"
#include "dg/llvm/ControlDependence/LLVMControlDependenceAnalysisOptions.h"

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/Support/CommandLine.h>
SILENCE_LLVM_WARNINGS_POP

#include "dg/tools/llvm-slicer.h"
#include "dg/tools/llvm-slicer-utils.h"

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
SlicerOptions parseSlicerOptions(int argc, char *argv[], bool requireCrit, bool inputFileRequired) {
    llvm::cl::opt<std::string> outputFile("o",
        llvm::cl::desc("Save the output to given file. If not specified,\n"
                       "a .sliced suffix is used with the original module name."),
        llvm::cl::value_desc("filename"), llvm::cl::init(""), llvm::cl::cat(SlicingOpts));


    llvm::cl::opt<std::string> inputFile(llvm::cl::Positional,
        llvm::cl::desc("<input file>"), llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

    if (inputFileRequired) {
        inputFile.setNumOccurrencesFlag(llvm::cl::Required);
    }

    llvm::cl::opt<std::string> slicingCriteria("sc",
        llvm::cl::desc("Slicing criterion\n"
                       "S ::= file1,file2#line1,line2#[var1,fun1],[var2,fun2]\n"
                       "S[&S];S[&S]\n"),
                       llvm::cl::value_desc("crit"),
                       llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<std::string> legacySlicingCriteria("c",
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

    llvm::cl::opt<std::string> legacySecondarySlicingCriteria("2c",
        llvm::cl::desc("Set secondary slicing criterion. The criterion is a call\n"
                       "to a given function. If just a name of the function is\n"
                       "given, it is a 'control' slicing criterion. If there is ()\n"
                       "appended, it is 'data' slicing criterion. E.g. foo means\n"
                       "control secondary slicing criterion, foo() means data\n"
                       "data secondary slicing criterion.\n"),
                       llvm::cl::value_desc("crit"),
                       llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> removeSlicingCriteria("remove-slicing-criteria",
        llvm::cl::desc("By default, slicer keeps also slicing criteria\n"
                       "in the sliced program. This switch makes slicer to remove\n"
                       "also the criteria (i.e. behave like Weisser's algorithm)"),
                       llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<std::string> preservedFuns("preserved-functions",
        llvm::cl::desc("Do not slice bodies of the given functions.\n"
                       "The argument is a comma-separated list of functions.\n"),
                       llvm::cl::value_desc("funs"),
                       llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> interprocCd("interproc-cd",
        llvm::cl::desc("Compute interprocedural dependencies that cover, e.g.,\n"
                       "calls calls to exit() from inside of procedures. Default: true.\n"),
                       llvm::cl::init(true), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> cdaPerInstr("cda-per-inst",
        llvm::cl::desc("Compute control dependencies per instruction (the default\n"
                       "is per basic block)\n"),
                       llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> icfgCD("cda-icfg",
        llvm::cl::desc("Compute control dependencies on interprocedural CFG.\n"
                       "Default: false (interprocedral CD are computed by\n"
                       "a separate analysis.\n"),
                       llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<uint64_t> ptaFieldSensitivity("pta-field-sensitive",
        llvm::cl::desc("Make PTA field sensitive/insensitive. The offset in a pointer\n"
                       "is cropped to Offset::UNKNOWN when it is greater than N bytes.\n"
                       "Default is full field-sensitivity (N = Offset::UNKNOWN).\n"),
                       llvm::cl::value_desc("N"), llvm::cl::init(dg::Offset::UNKNOWN),
                       llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<dg::dda::UndefinedFunsBehavior> undefinedFunsBehavior("undefined-funs",
        llvm::cl::desc("Set the behavior of undefined functions\n"),
        llvm::cl::values(
            clEnumValN(dg::dda::PURE,       "pure",
                       "Assume that undefined functions do not read nor write memory"),
            clEnumValN(dg::dda::WRITE_ANY,  "write-any",
                       "Assume that undefined functions may write any memory"),
            clEnumValN(dg::dda::READ_ANY,   "read-any",
                       "Assume that undefined functions may read any memory"),
            clEnumValN(dg::dda::READ_ANY | dg::dda::WRITE_ANY,   "rw-any",
                       "Assume that undefined functions may read and write any memory"),
            clEnumValN(dg::dda::WRITE_ARGS, "write-args",
                       "Assume that undefined functions may write to arguments"),
            clEnumValN(dg::dda::READ_ARGS,  "read-args",
                       "Assume that undefined functions may read from arguments (default)"),
            clEnumValN(dg::dda::WRITE_ARGS | dg::dda::READ_ARGS,
                       "rw-args",  "Assume that undefined functions may read or write from/to arguments")
    #if LLVM_VERSION_MAJOR < 4
            , nullptr
    #endif
            ),
        llvm::cl::init(dg::dda::READ_ARGS), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<std::string> entryFunction("entry",
        llvm::cl::desc("Entry function of the program\n"),
                       llvm::cl::init("main"), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> forwardSlicing("forward",
        llvm::cl::desc("Perform forward slicing\n"),
                       llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> threads("consider-threads",
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
        llvm::cl::desc("Choose data dependence analysis to use:"),
        llvm::cl::values(
            clEnumValN(LLVMDataDependenceAnalysisOptions::AnalysisType::ssa,
                       "ssa", "MemorySSA DDA (the only option right now)")
    #if LLVM_VERSION_MAJOR < 4
            , nullptr
    #endif
            ),
        llvm::cl::init(LLVMDataDependenceAnalysisOptions::AnalysisType::ssa),
                       llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<dg::ControlDependenceAnalysisOptions::CDAlgorithm> cdAlgorithm("cda",
        llvm::cl::desc("Choose control dependencies algorithm:"),
        llvm::cl::values(
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::STANDARD,
                       "standard", "Ferrante's algorithm (default)"),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::STANDARD,
                       "classic", "Alias to \"standard\""),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::STANDARD,
                       "scd", "Alias to \"standard\""),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD,
                       "ntscd", "Non-termination sensitive control dependencies algorithm"),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD2,
                       "ntscd2", "Non-termination sensitive control dependencies algorithm (a different implementation)"),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD_RANGANATH,
                       "ntscd-ranganath",
                       "Non-termination sensitive control dependencies algorithm (the fixed version of the original Ranganath et al.'s algorithm)"),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD_RANGANATH_ORIG,
                       "ntscd-ranganath-orig",
                       "Non-termination sensitive control dependencies algorithm (the original (wrong) Ranganath et al.'s algorithm)"),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD_LEGACY,
                       "ntscd-legacy", "Non-termination sensitive control dependencies algorithm (legacy implementation)"),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::DOD_RANGANATH,
                       "dod-ranganath", "Decisive order dependencies algorithm by Ranganath et al. - fixed version (standalone - for debugging)"),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::DOD,
                       "dod", "Decisive order dependencies algorithm (standalone - for debugging)"),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::DODNTSCD,
                       "dod+ntscd", "NTSCD and DOD together"),
            clEnumValN(dg::ControlDependenceAnalysisOptions::CDAlgorithm::STRONG_CC,
                       "scc", "Use strong control closure algorithm")
    #if LLVM_VERSION_MAJOR < 4
            , nullptr
    #endif
             ),
        llvm::cl::init(dg::ControlDependenceAnalysisOptions::CDAlgorithm::STANDARD),
        llvm::cl::cat(SlicingOpts));

    llvm::cl::alias cdAlgAlias("cd-alg",
        llvm::cl::desc("Choose control dependencies algorithm to use"
                       "(this options is obsolete, it is alias to -cda):"),
        llvm::cl::aliasopt(cdAlgorithm),
        llvm::cl::cat(SlicingOpts));

    llvm::cl::alias cdaInterprocAlias("cda-interproc",
        llvm::cl::desc("Alias to interproc-cd"),
        llvm::cl::aliasopt(interprocCd),
        llvm::cl::cat(SlicingOpts));

    ////////////////////////////////////
    // ===-- End of the options --=== //
    ////////////////////////////////////

    // hide all options except ours options
    // this method is available since LLVM 3.7
#if ((LLVM_VERSION_MAJOR > 3)\
      || ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR >= 7)))
    llvm::cl::HideUnrelatedOptions(SlicingOpts);
#endif
    llvm::cl::ParseCommandLineOptions(argc, argv);

    if (requireCrit) {
        if (slicingCriteria.getNumOccurrences() +
            legacySlicingCriteria.getNumOccurrences() == 0) {
            llvm::errs() << "No slicing criteria specified (-sc or -c option)\n";
            std::exit(1);
        }
    }

    /// Fill the structure
    SlicerOptions options;

    options.inputFile = inputFile;
    options.outputFile = outputFile;
    options.slicingCriteria = slicingCriteria;
    options.legacySlicingCriteria = legacySlicingCriteria;
    options.legacySecondarySlicingCriteria = legacySecondarySlicingCriteria;
    options.preservedFunctions = splitList(preservedFuns);
    options.removeSlicingCriteria = removeSlicingCriteria;
    options.forwardSlicing = forwardSlicing;

    auto& dgOptions = options.dgOptions;
    auto& PTAOptions = dgOptions.PTAOptions;
    auto& DDAOptions = dgOptions.DDAOptions;
    auto& CDAOptions = dgOptions.CDAOptions;

    dgOptions.entryFunction = entryFunction;
    dgOptions.threads = threads;

    CDAOptions.algorithm = cdAlgorithm;
    CDAOptions.interprocedural = interprocCd;
    CDAOptions._icfg = icfgCD;
    CDAOptions.setNodePerInstruction(cdaPerInstr);

    addAllocationFuns(dgOptions, allocationFuns);

    PTAOptions.entryFunction = entryFunction;
    PTAOptions.fieldSensitivity = dg::Offset(ptaFieldSensitivity);
    PTAOptions.analysisType = ptaType;
    PTAOptions.threads = threads;

    DDAOptions.threads = threads;
    DDAOptions.entryFunction = entryFunction;
    DDAOptions.undefinedFunsBehavior = undefinedFunsBehavior;
    DDAOptions.analysisType = ddaType;

    return options;
}

