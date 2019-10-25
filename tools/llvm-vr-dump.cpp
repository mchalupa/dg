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

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#undef NDEBUG // we need dump methods
#include "dg/llvm/ValueRelations/ValueRelations.h"
#include "dg/llvm/ValueRelations/getValName.h"

#include "TimeMeasure.h"

using namespace dg::analysis;
using llvm::errs;

/*
static bool verbose = false;
static const char *entryFunc = "main";
*/

llvm::cl::opt<bool> todot("dot",
    llvm::cl::desc("Dump graph in grahviz format"), llvm::cl::init(false));

llvm::cl::opt<unsigned> max_iter("max-iter",
    llvm::cl::desc("Maximal number of iterations"), llvm::cl::init(0));

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


    LLVMValueRelations VR(M);

    tm.start();

    VR.build();
    VR.compute(max_iter);

    tm.stop();
    tm.report("INFO: Value Relations analysis took");

    std::cout << std::endl;

    if (todot) {
        std::cout << "digraph VR {\n";
        for (const auto& block : VR.getBlocks()) {
            for (const auto& loc : block.second->locations) {
                std::cout << "  NODE" << loc->id;
                std::cout << "[label=\"";
                std::cout << "\\n";
                loc->dump();
                std::cout << "\\n------ REL ------\\n";
                loc->relations.dump();
                std::cout << "\\n------ EQ ------\\n";
                loc->equalities.dump();
                std::cout << "\\n----- READS -----\\n";
                loc->reads.dump();
                std::cout << "\"];\n";
            }
        }

        for (const auto& block : VR.getBlocks()) {
            for (const auto& loc : block.second->locations) {
                for (const auto& succ : loc->successors) {
                    std::cout << "  NODE" << loc->id
                              << " -> NODE" << succ->target->id
                              << " [label=\"";
                    succ->op->dump();
                    std::cout << "\"];\n";
                }
            }
        }

        std::cout << "}\n";
    } else {
        for (auto& F : *M) {
            for (auto& B : F) {
                for (auto& I : B) {
                    auto loc = VR.getMapping(&I);
                    if (!loc)
                        continue;

                    std::cout << "==============================================\n";
                    std::cout << dg::debug::getValName(&I) << "\n";
                    std::cout << "==============================================";
                    std::cout << "\n------ REL ------\n";
                    loc->relations.dump();
                    std::cout << "\n------ EQ ------\n";
                    loc->equalities.dump();
                    std::cout << "\n----- READS -----\n";
                    loc->reads.dump();
                    std::cout << "\n";
                }
            }
        }
    }


    return 0;
}
