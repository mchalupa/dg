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
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include <iostream>
#include <fstream>

#include "args.h"
#include "ControlFlowGraph.h"
#include "dg/analysis/PointsTo/PointerAnalysisFI.h"
#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"

#include <iostream>

using namespace std;

int main(int argc, const char *argv[])
{
    using namespace llvm;
    std::string module;
    std::string graphvizFileName;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    try {
        Arguments arguments;
        arguments.add('p', "path", "Path to llvm bitcode file", true);
        arguments.add('o', "outputFile", "Path to dot graphviz output file", true);
        arguments.parse(argc, argv);
        if (arguments("path")) {
            module = arguments("path").getString();
        }
        if (arguments("outputFile")) {
            graphvizFileName = arguments("outputFile").getString();
        }
    } catch (std::exception &e) {
        cout << "\nException: " << e.what() << endl;
    }
    std::unique_ptr<Module> M = llvm::parseIRFile(module.c_str(), SMD, context);

    if (!M) {
        llvm::errs() << "Failed parsing '" << module << "' file:\n";
        SMD.print(argv[0], errs());
        return 1;
    }

    dg::LLVMPointerAnalysis pointsToAnalysis(M.get());
    pointsToAnalysis.run<dg::analysis::pta::PointerAnalysisFI>();
    ControlFlowGraph graph(M.get(), &pointsToAnalysis);
    graph.build();
    graph.traverse();

    if (graphvizFileName == "") {
        std::cout << graph << std::endl;
    } else {
        std::ofstream graphvizFile(graphvizFileName);
        graphvizFile << graph << std::endl;
    }


    return 0;
}

