#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#undef NDEBUG // we need dump methods
#include "dg/llvm/ValueRelations/GraphBuilder.h"
#include "dg/llvm/ValueRelations/GraphElements.h"
#include "dg/llvm/ValueRelations/RelationsAnalyzer.h"
#include "dg/llvm/ValueRelations/StructureAnalyzer.h"
#include "dg/llvm/ValueRelations/getValName.h"

#include "dg/tools/TimeMeasure.h"

using namespace dg::vr;
using llvm::errs;

llvm::cl::opt<bool> todot("dot", llvm::cl::desc("Dump graph in grahviz format"),
                          llvm::cl::init(false));

llvm::cl::opt<unsigned> max_iter("max-iter",
                                 llvm::cl::desc("Maximal number of iterations"),
                                 llvm::cl::init(20));

llvm::cl::opt<std::string> inputFile(llvm::cl::Positional, llvm::cl::Required,
                                     llvm::cl::desc("<input file>"),
                                     llvm::cl::init(""));

int main(int argc, char *argv[]) {
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;

    llvm::cl::ParseCommandLineOptions(argc, argv);

    if (inputFile.empty()) {
        errs() << "Usage: % IR_module\n";
        return 1;
    }

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 5))
    M = llvm::ParseIRFile(inputFile, SMD, context);
#else
    auto _M = llvm::parseIRFile(inputFile, SMD, context);
    // _M is unique pointer, we need to get Module *
    M = _M.get();
#endif

    if (!M) {
        llvm::errs() << "Failed parsing '" << inputFile << "' file:\n";
        SMD.print(argv[0], errs());
        return 1;
    }

    dg::debug::TimeMeasure tm;

    tm.start();

    // perform preparations and analysis
    VRCodeGraph codeGraph;

    GraphBuilder gb(*M, codeGraph);
    gb.build();

    StructureAnalyzer structure(*M, codeGraph);
    structure.analyzeBeforeRelationsAnalysis();

    RelationsAnalyzer ra(*M, codeGraph, structure);
    unsigned num_iter = ra.analyze(max_iter);
    structure.analyzeAfterRelationsAnalysis();
    // call to analyzeAfterRelationsAnalysis is unnecessary, but better for
    // testing end analysis

    tm.stop();
    tm.report("INFO: Value Relations analysis took");
    std::cerr << "INFO: The analysis made " << num_iter << " passes."
              << std::endl;
    std::cerr << std::endl;

    if (todot) {
        std::cout << "digraph VR {\n";
        for (auto &loc : codeGraph) {
            std::cout << "  NODE" << loc.id;
            std::cout << "[shape=box, margin=0.15, label=\"";
            std::cout << "LOCATION " << loc.id << "\\n";
            std::cout << loc.relations;
            std::cout << "\"];\n";
        }

        unsigned dummyIndex = 0;
        for (auto &loc : codeGraph) {
            for (const auto &succ : loc.successors) {
                if (succ->target)
                    std::cout << "  NODE" << loc.id << " -> NODE"
                              << succ->target->id;
                else {
                    std::cout << "DUMMY_NODE" << ++dummyIndex << std::endl;
                    std::cout << "  NODE" << loc.id << " -> DUMMY_NODE"
                              << dummyIndex;
                }
                std::cout << " [label=\"";
                succ->op->dump();
                std::cout << "\", color=";
                switch (succ->type) {
                case EdgeType::TREE:
                    std::cout << "darkgreen";
                    break;
                case EdgeType::FORWARD:
                    std::cout << "blue";
                    break;
                case EdgeType::BACK:
                    std::cout << "red";
                    break;
                case EdgeType::DEFAULT:
                    std::cout << "pink";
                    break;
                }
                std::cout << "];\n";
            }
        }
        std::cout << "}\n";
    }

    return 0;
}
