#include "dg/PointerAnalysis/PointerAnalysisFI.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

#include "../lib/llvm/ControlDependence/legacy/GraphBuilder.h"
#include "../lib/llvm/ControlDependence/legacy/NTSCD.h"

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
SILENCE_LLVM_WARNINGS_POP

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
    llvm::cl::opt<bool> threads("consider-threads",
                                llvm::cl::desc("Consider threads are in input file (default=false)."),
                                llvm::cl::init(false));

    llvm::cl::opt<bool> withpta("pta",
                                llvm::cl::desc("Run pointer analysis to ger reachable functions (default=false)."),
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

    std::unique_ptr<dg::LLVMPointerAnalysis> PTA;
    if (withpta) {
        dg::LLVMPointerAnalysisOptions opts;
        opts.setEntryFunction("main");
        opts.analysisType = dg::LLVMPointerAnalysisOptions::AnalysisType::fi;
        opts.threads = threads;
        opts.setFieldSensitivity(dg::Offset::UNKNOWN);

        PTA.reset(new dg::DGLLVMPointerAnalysis(M.get(), opts));
        PTA->run();
    }

    dg::llvmdg::legacy::NTSCD controlDependencyAnalysis(M.get(), {}, PTA.get());
    controlDependencyAnalysis.compute();

    if (graphVizFileName == "") {
        controlDependencyAnalysis.dump(std::cout);
    } else {
        std::ofstream graphvizFile(graphVizFileName);
        controlDependencyAnalysis.dump(graphvizFile);
    }
    return 0;
}
