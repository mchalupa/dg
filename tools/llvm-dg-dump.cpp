#include <assert.h>
#include <cstdio>

#include <set>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Bitcode/ReaderWriter.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include "llvm/LLVMDependenceGraph.h"
#include "llvm/PointsTo.h"
#include "llvm/ReachingDefs.h"
#include "llvm/DefUse.h"
#include "llvm/Slicer.h"
#include "DG2Dot.h"
#include "Utils.h"

using namespace dg;
using llvm::errs;

static std::ostream& printLLVMVal(std::ostream& os, const llvm::Value *val)
{
    if (!val) {
        os << "(null)";
        return os;
    }

    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    if (llvm::isa<llvm::Function>(val)) {
        ro << "FUNC " << val->getName().data();
    } else if (llvm::isa<llvm::BasicBlock>(val)) {
        ro << "label " << val->getName().data();
    } else {
        ro << *val;
    }

    ro.flush();

    // break the string if it is too long
    std::string str = ostr.str();
    if (str.length() > 100) {
        str.resize(40);
    }

    // escape the "
    size_t pos = 0;
    while ((pos = str.find('"', pos)) != std::string::npos) {
        str.replace(pos, 1, "\\\"");
        // we replaced one char with two, so we must shift after the new "
        pos += 2;
    }

    os << str;

    return os;
}

static std::ostream& operator<<(std::ostream& os, const analysis::Offset& off)
{
    if (off.offset == UNKNOWN_OFFSET)
        os << "UNKNOWN";
    else
        os << off.offset;
}

static bool debug_def = false;
static bool debug_ptr = false;

static bool checkNode(std::ostream& os, LLVMNode *node)
{
    bool err = false;
    const llvm::Value *val = node->getKey();

    if (!val) {
        os << "\\nERR: no value in node";
        return true;
    }

    if (!node->getBBlock()
        && !llvm::isa<llvm::Function>(val)
        && !llvm::isa<llvm::GlobalVariable>(val)) {
        err = true;
        os << "\\nERR: no BB";
    }

    LLVMNode *s = node->getSuccessor();
    LLVMNode *p = node->getPredcessor();
    if (s) {
        if (s->getPredcessor() != node) {
            os << "\\nERR: wrong predcessor";
            err = true;
        }

        if(s->getBBlock() != node->getBBlock()) {
            os << "\\nERR: succ BB mismatch";
            err = true;
        }
    }

    if (p) {
        if (p->getSuccessor() != node) {
            os << "\\nERR: wrong successor";
            err = true;
        }

        if(p->getBBlock() != node->getBBlock()) {
            os << "\\nERR: pred BB mismatch";
            err = true;
        }
    }

    if (debug_ptr) {
        const analysis::PointsToSetT& ptsto = node->getPointsTo();
        if (ptsto.empty() && val->getType()->isPointerTy()) {
            os << "\\lERR: pointer without pointsto set";
            err = true;
        } else
            os << "\\l -- points-to info --";

        for (auto it : ptsto) {
            os << "\\l[";
            if (it.isUnknown())
                os << "unknown";
            else if (it.obj->isUnknown())
                os << "unknown mem";
            else
                printLLVMVal(os, it.obj->node->getKey());
            os << "] + " << it.offset;
        }

        analysis::MemoryObj *mo = node->getMemoryObj();
        if (mo) {
            for (auto it : mo->pointsTo) {
                for(auto it2 : it.second) {
                    os << "\\lmem: [" << it.first << "] -> [";
                    if (it2.isUnknown())
                        os << "unknown";
                    else if (it2.obj->isUnknown())
                        os << "unknown mem";
                    else
                        printLLVMVal(os, it2.obj->node->getKey());
                    os << "] + " << it2.offset;
                }
            }
        }
    }

    if (debug_def) {
        analysis::DefMap *df = node->getData<analysis::DefMap>();
        if (df) {
            os << "\\l -- reaching defs info --";
            for (auto it : df->getDefs()) {
                for (auto v : it.second) {
                    os << "\\l: [";
                    if (it.first.obj->isUnknown())
                        os << "[unknown";
                    else
                        printLLVMVal(os, it.first.obj->node->getKey());
                    os << "] + " << it.first.offset << " @ ";
                    printLLVMVal(os, v->getKey());
                }
            }
        }
    }

    return err;
}

static const char *slicing_criterion;
static bool mark_only = false;

int main(int argc, char *argv[])
{
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    llvm::Module *M;
    const char *module = NULL;

    using namespace debug;
    uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-no-control") == 0) {
            opts &= ~PRINT_CD;
        } else if (strcmp(argv[i], "-no-data") == 0) {
            opts &= ~PRINT_DD;
        } else if (strcmp(argv[i], "-nocfg") == 0) {
            opts &= ~PRINT_CFG;
        } else if (strcmp(argv[i], "-call") == 0) {
            opts |= PRINT_CALL;
        } else if (strcmp(argv[i], "-cfgall") == 0) {
            opts |= PRINT_CFG;
            opts |= PRINT_REV_CFG;
        } else if (strcmp(argv[i], "-pss") == 0) {
            opts |= PRINT_PSS;
        } else if (strcmp(argv[i], "-def") == 0) {
            debug_def = true;
        } else if (strcmp(argv[i], "-ptr") == 0) {
            debug_ptr = true;
        } else if (strcmp(argv[i], "-slice") == 0) {
            slicing_criterion = argv[++i];
        } else if (strcmp(argv[i], "-mark") == 0) {
            mark_only = true;
            slicing_criterion = argv[++i];
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module [output_file]\n";
        return 1;
    }

    M = llvm::ParseIRFile(module, SMD, context);
    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    debug::TimeMeasure tm;

    LLVMDependenceGraph d;
    d.build(M);

    analysis::LLVMPointsToAnalysis PTA(&d);

    tm.start();
    PTA.run();
    tm.stop();
    tm.report("INFO: Points-to analysis took");

    std::set<LLVMNode *> callsites;
    if (slicing_criterion) {
        const char *sc[] = {
            slicing_criterion,
            "klee_assume",
            NULL
        };

        tm.start();
        d.getCallSites(sc, &callsites);
        tm.stop();
        tm.report("INFO: Finding slicing criterions took");
    }


    analysis::LLVMReachingDefsAnalysis RDA(&d);
    tm.start();
    RDA.run();  // compute reaching definitions
    tm.stop();
    tm.report("INFO: Reaching defs analysis took");

    analysis::LLVMDefUseAnalysis DUA(&d);
    tm.start();
    DUA.run(); // add def-use edges according that
    tm.stop();
    tm.report("INFO: Adding Def-Use edges took");

    tm.start();
    // add post-dominator frontiers
    d.computePostDominators(true);
    tm.stop();
    tm.report("INFO: computing post-dominator frontiers took");

    if (slicing_criterion) {
        LLVMSlicer slicer;
        tm.start();

        if (strcmp(slicing_criterion, "ret") == 0) {
            if (mark_only)
                slicer.mark(d.getExit());
            else
                slicer.slice(&d, d.getExit());
        } else {
            if (callsites.empty()) {
                errs() << "ERR: slicing criterion not found: "
                       << slicing_criterion << "\n";
                exit(1);
            }

            uint32_t slid = 0;
            for (LLVMNode *start : callsites)
                slid = slicer.mark(start, slid);

            if (!mark_only)
               slicer.slice(&d, nullptr, slid);
        }

        // there's overhead but nevermind
        tm.stop();
        tm.report("INFO: Slicing took");

        if (!mark_only) {
            std::string fl(module);
            fl.append(".sliced");
            std::ofstream ofs(fl);
            llvm::raw_os_ostream output(ofs);

            auto st = slicer.getStatistics();
            errs() << "INFO: Sliced away " << st.second
                   << " from " << st.first << " nodes\n";

            llvm::WriteBitcodeToFile(M, output);
        }
    }

    debug::DG2Dot<LLVMNode> dump(&d, opts);

    dump.printKey = printLLVMVal;
    dump.checkNode = checkNode;
    const std::map<const llvm::Value *,
                   LLVMDependenceGraph *>& CF = getConstructedFunctions();

    dump.start();

    for (auto F : CF) {
        dump.dumpSubgraphStart(F.second, F.first->getName().data());

        for (auto B : F.second->getConstructedBlocks()) {
            dump.dumpBBlock(B.second);
        }

        for (auto B : F.second->getConstructedBlocks()) {
            dump.dumpBBlockEdges(B.second);
        }

        dump.dumpSubgraphEnd(F.second);
    }

    dump.end();

    return 0;
}
