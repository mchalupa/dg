#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "../lib/llvm/ControlDependence/GraphBuilder.h"
#include "../lib/llvm/ControlDependence/NonTerminationSensitiveControlDependencyAnalysis.h"

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
#include <llvm/Support/SourceMgr.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include <fstream>

int main(int argc, const char *argv[]) {
    using namespace std;
    using namespace llvm;


    llvm::cl::opt<string> OutputFilename("o",
                                         cl::desc("Specify output filename"),
                                         cl::value_desc("filename"),
                                         cl::init(""));

    llvm::cl::opt<std::string> inputFile(cl::Positional,
                                         cl::Required,
                                         cl::desc("<input file>"),
                                         cl::init(""));
    llvm::cl::opt<bool> threads("threads",
                                llvm::cl::desc("Consider threads are in input file (default=false)."),
                                llvm::cl::init(false));
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;

    cl::ParseCommandLineOptions(argc, argv);

    string module = inputFile;
    string graphVizFileName = OutputFilename;


    std::unique_ptr<Module> M = llvm::parseIRFile(module.c_str(), SMD, context);

    if (!M) {
        llvm::errs() << "Failed parsing '" << module << "' file:\n";
        SMD.print(argv[0], errs());
        return 1;
    }

    dg::LLVMPointerAnalysisOptions opts;
    opts.setEntryFunction("main");
    opts.analysisType = dg::LLVMPointerAnalysisOptions::AnalysisType::fi;
    opts.threads = threads;
    opts.setFieldSensitivity(dg::Offset::UNKNOWN);

    dg::DGLLVMPointerAnalysis pointsToAnalysis(M.get(), opts);
    pointsToAnalysis.run();

    dg::cd::NonTerminationSensitiveControlDependencyAnalysis controlDependencyAnalysis(M.get()->getFunction("main"), &pointsToAnalysis);
    controlDependencyAnalysis.computeDependencies();

    if (graphVizFileName == "") {
        controlDependencyAnalysis.dump(std::cout);
    } else {
        std::ofstream graphvizFile(graphVizFileName);
        controlDependencyAnalysis.dump(graphvizFile);
    }
    return 0;
}
