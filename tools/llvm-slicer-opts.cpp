#include "analysis/Offset.h"
#include "llvm/LLVMDependenceGraph.h"
#include "llvm/LLVMDependenceGraphBuilder.h"
#include "llvm/analysis/PointsTo/LLVMPointerAnalysisOptions.h"
#include "llvm/analysis/ReachingDefinitions/LLVMReachingDefinitionsAnalysisOptions.h"

#include <llvm/Support/CommandLine.h>

#include "llvm-slicer.h"
#include "git-version.h"

using dg::analysis::LLVMPointerAnalysisOptions;
using dg::analysis::LLVMReachingDefinitionsAnalysisOptions;

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
    
    llvm::cl::opt<bool> removeSlicingCriteria("remove-slicing-criteria",
        llvm::cl::desc("By default, slicer keeps also calls to the slicing criteria\n"
                       "in the sliced program. This switch makes slicer to remove\n"
                       "also the calls (i.e. behave like Weisser's algorithm)"),
                       llvm::cl::init(false), llvm::cl::cat(SlicingOpts));
    
    llvm::cl::opt<uint64_t> ptaFieldSensitivity("pta-field-sensitive",
        llvm::cl::desc("Make PTA field sensitive/insensitive. The offset in a pointer\n"
                       "is cropped to Offset::UNKNOWN when it is greater than N bytes.\n"
                       "Default is full field-sensitivity (N = Offset::UNKNOWN).\n"),
                       llvm::cl::value_desc("N"), llvm::cl::init(dg::analysis::Offset::UNKNOWN),
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
    
    llvm::cl::opt<LLVMPointerAnalysisOptions::AnalysisType> ptaType("pta",
        llvm::cl::desc("Choose pointer analysis to use:"),
        llvm::cl::values(
            clEnumValN(LLVMPointerAnalysisOptions::AnalysisType::fi, "fi", "Flow-insensitive PTA (default)"),
            clEnumValN(LLVMPointerAnalysisOptions::AnalysisType::fs, "fs", "Flow-sensitive PTA"),
            clEnumValN(LLVMPointerAnalysisOptions::AnalysisType::inv, "inv", "PTA with invalidate nodes")
    #if LLVM_VERSION_MAJOR < 4
            , nullptr
    #endif
            ),
        llvm::cl::init(LLVMPointerAnalysisOptions::AnalysisType::fi), llvm::cl::cat(SlicingOpts));
    
    llvm::cl::opt<LLVMReachingDefinitionsAnalysisOptions::AnalysisType> rdaType("rda",
        llvm::cl::desc("Choose reaching definitions analysis to use:"),
        llvm::cl::values(
            clEnumValN(LLVMReachingDefinitionsAnalysisOptions::AnalysisType::dense, "dense", "Dense RDA (default)"),
            clEnumValN(LLVMReachingDefinitionsAnalysisOptions::AnalysisType::ss,    "ss",    "Semi-sparse RDA")
    #if LLVM_VERSION_MAJOR < 4
            , nullptr
    #endif
            ),
        llvm::cl::init(LLVMReachingDefinitionsAnalysisOptions::AnalysisType::dense), llvm::cl::cat(SlicingOpts));
    
    llvm::cl::opt<dg::CD_ALG> cdAlgorithm("cd-alg",
        llvm::cl::desc("Choose control dependencies algorithm to use:"),
        llvm::cl::values(
            clEnumValN(dg::CD_ALG::CLASSIC , "classic", "Ferrante's algorithm (default)"),
            clEnumValN(dg::CD_ALG::CONTROL_EXPRESSION, "ce", "Control expression based (experimental)")
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
    options.removeSlicingCriteria = removeSlicingCriteria;
    options.forwardSlicing = forwardSlicing;

    options.dgOptions.entryFunction = entryFunction;
    options.dgOptions.PTAOptions.entryFunction = entryFunction;
    options.dgOptions.PTAOptions.fieldSensitivity
                                    = dg::analysis::Offset(ptaFieldSensitivity);
    options.dgOptions.PTAOptions.analysisType = ptaType;

    options.dgOptions.RDAOptions.entryFunction = entryFunction;
    options.dgOptions.RDAOptions.strongUpdateUnknown = rdaStrongUpdateUnknown;
    options.dgOptions.RDAOptions.undefinedArePure = undefinedArePure;
    options.dgOptions.RDAOptions.analysisType = rdaType;

    // FIXME: add classes for CD and DEF-USE settings
    options.dgOptions.cdAlgorithm = cdAlgorithm;
    options.dgOptions.DUUndefinedArePure = undefinedArePure;

    return options;
}

