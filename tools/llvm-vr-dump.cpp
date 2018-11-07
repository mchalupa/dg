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

/*
#include "dg/analysis/PointsTo/PointerAnalysisFI.h"
#include "dg/analysis/PointsTo/PointerAnalysisFS.h"
#include "dg/analysis/PointsTo/Pointer.h"

#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"
*/
#include "dg/llvm/analysis/ValueRelations/ValueRelations.h"

#include "TimeMeasure.h"

using namespace dg::analysis;
using llvm::errs;

static bool todot = true;
/*
static bool verbose = false;
static const char *entryFunc = "main";

*/
static std::string
getInstName(const llvm::Value *val)
{
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    assert(val);
    ro << *val;
    ro.flush();

    // break the string if it is too long
    return ostr.str();
}

/*
static void printVRNodeType(enum VRNodeType type)
{
#define ELEM(t) case(t): do {printf("%s", #t); }while(0); break;
    switch(type) {
        ELEM(VRNodeType::FORGET)
        ELEM(VRNodeType::READ)
        ELEM(VRNodeType::VALUE)
        ELEM(VRNodeType::RELATION)
        default:
            printf("unknown type");
    };
#undef ELEM
}
*/

int main(int /*argc*/, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;

    const char *modulepath = nullptr;

    // parse options
    /*
    for (int i = 1; i < argc; ++i) {
        // run given points-to analysis
        if (strcmp(argv[i], "-pta") == 0) {
            if (strcmp(argv[i+1], "fs") == 0)
                type = FLOW_SENSITIVE;
        } else if (strcmp(argv[i], "-dot") == 0) {
            todot = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-entry") == 0) {
            entryFunc = argv[i+1];
        } else {
            modulepath = argv[i];
        }
        modulepath = argv[i];
    }
    */
    modulepath = argv[1];

    if (!modulepath) {
        //errs() << "Usage: % IR_module [-pts fs|fi] [-dot] [-v] [output_file]\n";
        errs() << "Usage: % IR_module\n";
        return 1;
    }

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 5))
    M = llvm::ParseIRFile(modulepath, SMD, context);
#else
    auto _M = llvm::parseIRFile(modulepath, SMD, context);
    // _M is unique pointer, we need to get Module *
    M = _M.get();
#endif

    if (!M) {
        llvm::errs() << "Failed parsing '" << modulepath << "' file:\n";
        SMD.print(argv[0], errs());
        return 1;
    }

    //debug::TimeMeasure tm;

    LLVMValueRelations VR(M);

    //tm.start();

    VR.build();
    VR.compute();
    //R.dump();

    std::cout << std::endl;

    if (todot) {
        std::cout << "digraph VR {\n";
        for (const auto& block : VR.getBlocks()) {
            for (const auto& loc : block.second->locations) {
                std::cout << "  NODE" << loc->id;
                std::cout << "[label=\"";
                std::cout << loc->id;
                if (loc->val)
                    std::cout << getInstName(loc->val);
                std::cout << "\\n";
                //loc->dump();
                std::cout << "\\n------ REL ------\\n";
                loc->relations.dump();
                std::cout << "\\n------ EQ ------\\n";
                loc->equalities.dump();
                std::cout << "\"];\n";
            }
        }

        for (const auto& block : VR.getBlocks()) {
            for (const auto& loc : block.second->locations) {
                for (const auto& succ : loc->successors) {
                    std::cout << "  NODE" << loc->id
                              << " -> NODE" << succ->target->id
                              << " [label=\"";
                    succ->info.dump();
                    std::cout << "\"];\n";
                }
            }
        }



        /*
        for (const auto& nd : VR.getNodes()) {
        }
        */
        std::cout << "}\n";
    }

    //dumpVR(&VR, todot);
    //tm.report("INFO: Value Relations analysis took");


    return 0;
}
