#include <assert.h>
#include <cstdio>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>

#include <iostream>
#include <sstream>
#include <string>
#include "llvm/LLVMDependenceGraph.h"
#include "llvm/PointsTo.h"
#include "llvm/DefUse.h"
#include "DG2Dot.h"

#include "analysis/Slicing.h"

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

    if (llvm::isa<llvm::Function>(val))
        ro << "ENTRY " << val->getName();
    else {
        ro << *val;
    }

    // break the string if it is too long
    std::string str = ostr.str();
    if (str.length() > 100) {
        str.resize(50);
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

    if (!node->getBasicBlock()
        && !llvm::isa<llvm::Function>(val)) {
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

        if(s->getBasicBlock() != node->getBasicBlock()) {
            os << "\\nERR: succ BB mismatch";
            err = true;
        }
    }

    if (p) {
        if (p->getSuccessor() != node) {
            os << "\\nERR: wrong successor";
            err = true;
        }

        if(p->getBasicBlock() != node->getBasicBlock()) {
            os << "\\nERR: pred BB mismatch";
            err = true;
        }
    }

    if (node->hasUnknownValue()) {
        os << "\\lUNKNOWN VALUE";
    } else {
        if (debug_ptr) {
            const analysis::PointsToSetT& ptsto = node->getPointsTo();
            if (ptsto.empty() && val->getType()->isPointerTy()) {
                os << "\\lERR: pointer without pointsto set";
                err = true;
            }

            for (auto it : ptsto) {
                os << "\\lPTR: [";
                if (it.obj->isUnknown())
                    os << "unknown";
                else
                    printLLVMVal(os, it.obj->node->getKey());
                os << "] + " << it.offset;
            }

            analysis::MemoryObj *mo = node->getMemoryObj();
            if (mo) {
                for (auto it : mo->pointsTo) {
                    for(auto it2 : it.second) {
                        os << "\\l--MEMPTR: [" << it.first << "] -> ";
                        if (it2.obj->isUnknown())
                            os << "[unknown";
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
                for (auto it : df->getDefs()) {
                    for (auto v : it.second) {
                        os << "\\l--DEF: [";
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
    }

    return err;
}

int main(int argc, char *argv[])
{
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    std::unique_ptr<llvm::Module> M;
    const char *module = NULL;

    using namespace debug;
    uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-no-control") == 0) {
            opts &= ~PRINT_CD;
        } else if (strcmp(argv[i], "-no-data") == 0) {
            opts &= ~PRINT_DD;
        } else if (strcmp(argv[i], "-cfg") == 0) {
            opts |= PRINT_CFG;
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
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module [output_file]\n";
        return 1;
    }

    M = llvm::parseIRFile(module, SMD, context);
    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    LLVMDependenceGraph d;
    d.build(&*M);

    analysis::Slicer<LLVMNode> slicer;
    slicer.slice(d.getExit());

    debug::DG2Dot<LLVMNode> dump(&d, opts);

    dump.printKey = printLLVMVal;
    dump.checkNode = checkNode;

    dump.dump("/dev/stdout", d.getEntryBB());

    return 0;
}
