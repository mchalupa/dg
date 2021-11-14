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

#include "dg/llvm/ValueRelations/GraphBuilder.h"
#include "dg/llvm/ValueRelations/GraphElements.h"
#include "dg/llvm/ValueRelations/RelationsAnalyzer.h"
#include "dg/llvm/ValueRelations/StructureAnalyzer.h"
#include "dg/llvm/ValueRelations/getValName.h"

#include "dg/util/TimeMeasure.h"

using namespace dg::vr;
using llvm::errs;

llvm::cl::opt<bool> todot("dot", llvm::cl::desc("Dump graph in grahviz format"),
                          llvm::cl::init(false));

llvm::cl::opt<bool> joins("joins", llvm::cl::desc("Dump join informations"),
                          llvm::cl::init(false));

llvm::cl::opt<unsigned> max_iter("max-iter",
                                 llvm::cl::desc("Maximal number of iterations"),
                                 llvm::cl::init(20));

llvm::cl::opt<std::string> inputFile(llvm::cl::Positional, llvm::cl::Required,
                                     llvm::cl::desc("<input file>"),
                                     llvm::cl::init(""));

std::string node(const VRLocation &loc) {
    return "  NODE" + std::to_string(loc.id);
}

std::string node(unsigned i) { return "  DUMMY_NODE" + std::to_string(i); }

template <typename N1, typename N2>
std::string edge(const N1 &n1, const N2 &n2) {
    return node(n1) + "  ->" + node(n2);
}

std::string edgeTypeToColor(EdgeType type) {
    switch (type) {
    case EdgeType::TREE:
        return "darkgreen";
    case EdgeType::FORWARD:
        return "blue";
    case EdgeType::BACK:
        return "red";
    case EdgeType::DEFAULT:
        return "pink";
    }
    assert(0 && "unreach");
    abort();
}

void dumpNodes(const VRCodeGraph &codeGraph) {
    for (auto &loc : codeGraph) {
        std::cout << node(loc);
        std::cout << "[shape=box, margin=0.15, label=\"";
        std::cout << "LOCATION " << loc.id << "\n";
#ifndef NDEBUG
        std::cout << loc.relations;
#endif
        std::cout << "  \"];\n";
    }
}

void dumpEdges(const VRCodeGraph &codeGraph) {
    unsigned dummyIndex = 0;
    for (const auto &loc : codeGraph) {
        for (const auto &succ : loc.successors) {
            if (succ->target)
                std::cout << edge(loc, *succ->target);
            else {
                std::cout << node(++dummyIndex) << "\n";
                std::cout << edge(loc, dummyIndex);
            }
            std::cout << " [label=\"";
#ifndef NDEBUG
            succ->op->dump();
#endif
            std::cout << "\", color=" << edgeTypeToColor(succ->type) << "];\n";
        }

        if (loc.isJustLoopJoin()) {
            for (auto e : loc.loopEnds) {
                std::cout << edge(loc, *e->target) << " [color=magenta];\n";
            }
        }

        if (joins && loc.join)
            std::cout << edge(loc, *loc.join) << " [color=pink];\n";
    }
}

void dotDump(const VRCodeGraph &codeGraph) {
    std::cout << "digraph VR {\n";

    dumpNodes(codeGraph);
    dumpEdges(codeGraph);

    std::cout << "}\n";
}

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
              << "\n";
    std::cerr << "\n";

    if (todot)
        dotDump(codeGraph);

    return 0;
}
