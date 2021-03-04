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

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif
SILENCE_LLVM_WARNINGS_POP

#undef NDEBUG // we need dump methods
#include "dg/llvm/ValueRelations/GraphBuilder.h"
#include "dg/llvm/ValueRelations/StructureAnalyzer.h"
#include "dg/llvm/ValueRelations/RelationsAnalyzer.h"
#include "dg/llvm/ValueRelations/getValName.h"

#include "dg/tools/TimeMeasure.h"

using namespace dg::vr;
using llvm::errs;

llvm::cl::opt<bool> todot("dot",
    llvm::cl::desc("Dump graph in grahviz format"), llvm::cl::init(false));

llvm::cl::opt<unsigned> max_iter("max-iter",
    llvm::cl::desc("Maximal number of iterations"), llvm::cl::init(100));

llvm::cl::opt<std::string> inputFile(llvm::cl::Positional, llvm::cl::Required,
    llvm::cl::desc("<input file>"), llvm::cl::init(""));

int main(int argc, char *argv[])
{
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
    std::map<const llvm::Instruction*, VRLocation*> locationMapping;
    std::map<const llvm::BasicBlock*, std::unique_ptr<VRBBlock>> blockMapping;

    GraphBuilder gb(*M, locationMapping, blockMapping);
    gb.build();

    StructureAnalyzer structure(*M, locationMapping, blockMapping);
    structure.analyzeBeforeRelationsAnalysis();

    RelationsAnalyzer ra(*M, locationMapping, blockMapping, structure);
    ra.analyze(max_iter);
    // call to analyzeAfterRelationsAnalysis is unnecessary
    // end analysis

    tm.stop();
    tm.report("INFO: Value Relations analysis took");

    std::cout << std::endl;

    if (todot) {
        std::cout << "digraph VR {\n";
        for (const auto& block : blockMapping) {
            for (const auto& loc : block.second->locations) {
                std::cout << "  NODE" << loc->id;
                std::cout << "[label=\"";
                std::cout << "LOCATION " << loc->id << "\\n";
                loc->relations.dump();
                std::cout << "\"];\n";
            }
        }

        unsigned dummyIndex = 0;
        for (const auto& block : blockMapping) {
            for (const auto& loc : block.second->locations) {
                for (const auto& succ : loc->successors) {
                    if (succ->target)
                        std::cout << "  NODE" << loc->id << " -> NODE" << succ->target->id;
                    else {
                        std::cout << "DUMMY_NODE" << ++dummyIndex << std::endl;
                        std::cout << "  NODE" << loc->id << " -> DUMMY_NODE" << dummyIndex;
                    }
                    std::cout << " [label=\"";
                    succ->op->dump();
                    std::cout << "\"];\n";
                }
            }
        }
        std::cout << "}\n";
    }

    return 0;
}
