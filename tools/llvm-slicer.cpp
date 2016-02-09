#include <assert.h>
#include <cstdio>
#include <cstring>

#include <set>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include "../git-version.h"

#include <llvm/Assembly/AssemblyAnnotationWriter.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Bitcode/ReaderWriter.h>

#include <iostream>
#include <fstream>
#include "llvm/LLVMDependenceGraph.h"
#include "llvm/PointsTo.h"
#include "llvm/ReachingDefs.h"
#include "llvm/DefUse.h"
#include "llvm/Slicer.h"
#include "Utils.h"

using namespace dg;
using llvm::errs;

enum {
    // annotate
    ANNOTATE                    = 1,
    // data dependencies
    ANNOTATE_DD                 = 1 << 1,
    // forward data dependencies
    ANNOTATE_FORWARD_DD         = 1 << 2,
    // control dependencies
    ANNOTATE_CD                 = 1 << 3,
    // points-to information
    ANNOTATE_PTR                = 1 << 4,
    // reaching definitions
    ANNOTATE_RD                 = 1 << 5,
    // post-dominators
    ANNOTATE_POSTDOM            = 1 << 6,
    // comment out nodes that will be sliced
    ANNOTATE_SLICE              = 1 << 7,
};

class CommentDBG : public llvm::AssemblyAnnotationWriter
{
    LLVMDependenceGraph *dg;
    uint32_t opts;

    void printPointer(const analysis::Pointer& ptr,
                      llvm::formatted_raw_ostream& os,
                      const char *prefix = "PTR: ", bool nl = true)
    {
        os << "  ; ";
        if (prefix)
            os << prefix;

        if (ptr.isKnown()) {
            os << *ptr.obj->node->getKey() << " + ";
            if (ptr.offset.isUnknown())
                os << "UNKNOWN";
            else
                os << *ptr.offset;
        } else
            os << "unknown";

        if (nl)
            os << "\n";
    }

    void emitNodeAnnotations(LLVMNode *node, llvm::formatted_raw_ostream& os)
    {
        if (opts & ANNOTATE_RD) {
            analysis::DefMap *df = node->getData<analysis::DefMap>();
            if (df) {
                for (auto it : *df) {
                    for (LLVMNode *d : it.second) {
                        printPointer(it.first, os, "RD: ", false);
                        os << " @ " << *d->getKey() << "(" << d << ")\n";
                    }
                }
            }

            LLVMDGParameters *params = node->getParameters();
            if (params) {
                for (auto it : *params) {
                    os << "  ; PARAMS: in " << it.second.in
                       << ", out " << it.second.out << "\n";

                    // dump edges for parameters
                    os <<"  ; in edges\n";
                    emitNodeAnnotations(it.second.in, os);
                    os << "  ; out edges\n";
                    emitNodeAnnotations(it.second.out, os);
                    os << "\n";
                }

                for (auto it = params->global_begin(), et = params->global_end();
                     it != et; ++it) {
                    os << "  ; PARAM GL: in " << it->second.in
                       << ", out " << it->second.out << "\n";

                    // dump edges for parameters
                    os << "  ; in edges\n";
                    emitNodeAnnotations(it->second.in, os);
                    os << "  ; out edges\n";
                    emitNodeAnnotations(it->second.out, os);
                    os << "\n";
                }
            }
        }

        if (opts & ANNOTATE_DD) {
            for (auto I = node->rev_data_begin(), E = node->rev_data_end();
                 I != E; ++I) {
                const llvm::Value *d = (*I)->getKey();
                os << "  ; DD: " << *d << "(" << d << ")\n";
            }
        }

        if (opts & ANNOTATE_FORWARD_DD) {
            for (auto I = node->data_begin(), E = node->data_end();
                 I != E; ++I) {
                const llvm::Value *d = (*I)->getKey();
                os << "  ; fDD: " << *d << "(" << d << ")\n";
            }
        }

        if (opts & ANNOTATE_CD) {
            for (auto I = node->rev_control_begin(), E = node->rev_control_end();
                 I != E; ++I) {
                const llvm::Value *d = (*I)->getKey();
                os << "  ; DD: " << *d << "\n";
            }
        }

        if (opts & ANNOTATE_PTR) {
            for (const analysis::Pointer& ptr : node->getPointsTo())
                printPointer(ptr, os);

            analysis::MemoryObj *mo = node->getMemoryObj();
            if (mo) {
                for (auto it : mo->pointsTo) {
                    for (const analysis::Pointer& ptr : it.second) {
                        os << "  ; PTR mem [" << *it.first << "] ";
                        printPointer(ptr, os, nullptr);
                    }
                }
            }
        }

        if (opts & ANNOTATE_SLICE)
            if (node->getSlice() == 0)
                os << "  ; x ";
    }

public:
    CommentDBG(LLVMDependenceGraph *dg, uint32_t o = ANNOTATE_DD)
        :dg(dg), opts(o) {}

    virtual void emitInstructionAnnot(const llvm::Instruction *I,
                                      llvm::formatted_raw_ostream& os)
    {
        if (opts == 0)
            return;

        LLVMNode *node = nullptr;
        for (auto it : getConstructedFunctions()) {
            LLVMDependenceGraph *sub = it.second;
            node = sub->getNode(I);
            if (node)
                break;
        }

        if (!node)
            return;

        emitNodeAnnotations(node, os);
    }

    virtual void emitBasicBlockStartAnnot(const llvm::BasicBlock *B,
                                          llvm::formatted_raw_ostream& os)
    {
        if (opts == 0)
            return;

        for (auto it : getConstructedFunctions()) {
            LLVMDependenceGraph *sub = it.second;
            auto cb = sub->getBlocks();
            LLVMBBlock *BB = cb[B];
            if (BB) {
                if (opts & ANNOTATE_POSTDOM) {
                    for (LLVMBBlock *p : BB->getPostDomFrontiers())
                        os << "  ; PDF: " << p->getKey()->getName() << "\n";

                    LLVMBBlock *P = BB->getIPostDom();
                    if (P && P->getKey())
                        os << "  ; iPD: " << P->getKey()->getName() << "\n";
                }

                // fixme - we have control dependencies in the BBs
            }
        }
    }
};

static void annotate(llvm::Module *M, const char *module_name,
                     LLVMDependenceGraph *d, uint32_t opts)
{
    // compose name
    std::string fl(module_name);
    fl.replace(fl.end() - 3, fl.end(), "-debug.ll");

    // open stream to write to
    std::ofstream ofs(fl);
    llvm::raw_os_ostream output(ofs);

    errs() << "INFO: Saving IR with annotations to " << fl << "\n";
    llvm::AssemblyAnnotationWriter *annot = new CommentDBG(d, opts);
    M->print(output, annot);

    delete annot;
}

static void computeGraphEdges(LLVMDependenceGraph *d)
{
    debug::TimeMeasure tm;

    analysis::LLVMReachingDefsAnalysis RDA(d);
    tm.start();
    RDA.run();  // compute reaching definitions
    tm.stop();
    tm.report("INFO: Reaching defs analysis took");

    analysis::LLVMDefUseAnalysis DUA(d);
    tm.start();
    DUA.run(); // add def-use edges according that
    tm.stop();
    tm.report("INFO: Adding Def-Use edges took");

    tm.start();
    // add post-dominator frontiers
    d->computePostDominators(true);
    tm.stop();
    tm.report("INFO: Computing post-dominator frontiers took");
}

static bool createEmptyMain(llvm::Module *M)
{
    llvm::Function *main_func = M->getFunction("main");
    if (!main_func) {
        errs() << "No main function found in module. This seems like bug since\n"
                  "here we should have the graph build from main\n";
        return false;
    }

    // delete old function body
    main_func->deleteBody();

    // create new function body that just returns
    llvm::LLVMContext& ctx = M->getContext();
    llvm::BasicBlock* blk = llvm::BasicBlock::Create(ctx, "entry", main_func);
    llvm::Type *Ty = main_func->getReturnType();
    llvm::Value *retval = nullptr;
    if (Ty->isIntegerTy())
        retval = llvm::ConstantInt::get(Ty, 0);
    llvm::ReturnInst::Create(ctx, retval, blk);

    return true;
}

static bool slice(llvm::Module *M, const char *module_name,
                  const char *slicing_criterion, uint32_t opts = 0)
{
    debug::TimeMeasure tm;
    LLVMDependenceGraph d;
    std::set<LLVMNode *> callsites;

    // build the graph
    d.build(&*M);

    // verify if the graph is built correctly
    // FIXME - do it optionally (command line argument)
    if (!d.verify())
        errs() << "ERR: verifying failed\n";

    analysis::LLVMPointsToAnalysis PTA(&d);

    tm.start();
    PTA.run();
    tm.stop();
    tm.report("INFO: Points-to analysis took");

    // we might added some new functions
    // so verify once again
    if (!d.verify())
        errs() << "ERR: verifying failed\n";

    // FIXME add command line switch -svcomp and
    // do this only with -svcomp switch
    const char *sc[] = {
        slicing_criterion,
        "klee_assume",
        NULL
    };

    // check for slicing criterion here, because
    // we might have built new subgraphs that contain
    // it during points-to analysis
    tm.start();
    bool ret = d.getCallSites(sc, &callsites);
    tm.stop();

    bool got_slicing_criterion = true;
    if (!ret) {
        if (strcmp(slicing_criterion, "ret") == 0) {
            callsites.insert(d.getExit());
        } else {
            errs() << "Did not find slicing criterion: "
                   << slicing_criterion << "\n";
            got_slicing_criterion = false;
        }
    }

    // if we found slicing criterion, compute the rest
    // of the graph. Otherwise just slice away the whole graph
    // Also count the edges when user wants to annotate
    // the file - due to debugging
    if (got_slicing_criterion || (opts & ANNOTATE))
        computeGraphEdges(&d);

    // don't go through the graph when we know the result:
    // only empty main will stay there. Just delete the body
    // of main and keep the return value
    if (!got_slicing_criterion)
        return createEmptyMain(M);

    LLVMSlicer slicer;
    uint32_t slid = 0xdead;

    // do not slice klee_assume at all
    // FIXME: do this optional
    slicer.keepFunctionUntouched("klee_assume");

    tm.start();
    for (LLVMNode *start : callsites)
        slid = slicer.mark(start, slid);

    // print debugging llvm IR if user asked for it
    if (opts & ANNOTATE)
        annotate(M, module_name, &d, opts);

    slicer.slice(&d, nullptr, slid);

    tm.stop();
    tm.report("INFO: Slicing dependence graph took");

    analysis::SlicerStatistics& st = slicer.getStatistics();
    errs() << "INFO: Sliced away " << st.nodesRemoved
           << " from " << st.nodesTotal << " nodes\n";

    return true;
}

static bool array_match(llvm::StringRef name, const char *names[])
{
    unsigned idx = 0;
    while(names[idx]) {
        if (name.equals(names[idx]))
            return true;
        ++idx;
    }

    return false;
}

static bool remove_unused_from_module(llvm::Module *M)
{
    using namespace llvm;
    // do not slice away these functions no matter what
    // FIXME do it a vector and fill it dynamically according
    // to what is the setup (like for sv-comp or general..)
    const char *keep[] = {"main", "klee_assume", NULL};

    // when erasing while iterating the slicer crashes
    // so set the to be erased values into container
    // and then erase them
    std::set<Function *> funs;
    std::set<GlobalVariable *> globals;
    auto cf = getConstructedFunctions();

    for (auto I = M->begin(), E = M->end(); I != E; ++I) {
        Function *func = &*I;
        if (array_match(func->getName(), keep))
            continue;

        // if the function is unused or we haven't constructed it
        // at all in dependence graph, we can remove it
        // (it may have some uses though - like when one
        // unused func calls the other unused func
        if (func->hasNUses(0))
            funs.insert(func);
    }

    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I) {
        GlobalVariable *gv = &*I;
        if (gv->hasNUses(0))
            globals.insert(gv);
    }

    for (Function *f : funs)
        f->eraseFromParent();
    for (GlobalVariable *gv : globals)
        gv->eraseFromParent();

    return (!funs.empty() || !globals.empty());
}

static void remove_unused_from_module_rec(llvm::Module *M)
{
    bool fixpoint;

    do {
        fixpoint = remove_unused_from_module(M);
    } while (fixpoint);
}

static bool verify_module(llvm::Module *M)
{
    // the verifyModule function returns false if there
    // are no errors
    return !llvm::verifyModule(*M, llvm::PrintMessageAction);
}

static bool write_module(llvm::Module *M, const char *module_name)
{
    // compose name
    std::string fl(module_name);
    fl.replace(fl.end() - 3, fl.end(), ".sliced");

    // open stream to write to
    std::ofstream ofs(fl);
    llvm::raw_os_ostream output(ofs);

    // write the module
    errs() << "INFO: saving sliced module to: " << fl.c_str() << "\n";
    llvm::WriteBitcodeToFile(M, output);

    return true;
}

int main(int argc, char *argv[])
{
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    llvm::Module *M;

    bool should_verify_module = true;
    bool remove_unused_only = false;
    const char *slicing_criterion = NULL;
    const char *module = NULL;
    uint32_t opts = 0;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0
            || strcmp(argv[i], "-version") == 0) {
            errs() << GIT_VERSION << "\n";
            exit(0);
        } else if (strcmp(argv[i], "-c") == 0
            || strcmp(argv[i], "-crit") == 0
            || strcmp(argv[i], "-slice") == 0){
            slicing_criterion = argv[++i];
        } else if (strcmp(argv[i], "-debug") == 0) {
            const char *arg = argv[++i];
            if (strcmp(arg, "dd") == 0)
                opts |= (ANNOTATE | ANNOTATE_DD);
            else if (strcmp(arg, "forward-dd") == 0)
                opts |= (ANNOTATE | ANNOTATE_FORWARD_DD);
            else if (strcmp(arg, "cd") == 0)
                opts |= (ANNOTATE | ANNOTATE_CD);
            else if (strcmp(arg, "ptr") == 0)
                opts |= (ANNOTATE | ANNOTATE_PTR);
            else if (strcmp(arg, "slice") == 0)
                opts |= (ANNOTATE | ANNOTATE_SLICE);
            else if (strcmp(arg, "rd") == 0)
                opts |= (ANNOTATE | ANNOTATE_RD);
            else if (strcmp(arg, "postdom") == 0)
                opts |= (ANNOTATE | ANNOTATE_POSTDOM);
        } else if (strcmp(argv[i], "-remove-unused-only") == 0) {
            remove_unused_only = true;
        } else if (strcmp(argv[i], "-dont-verify") == 0) {
            // for debugging
            should_verify_module = false;
        } else {
            module = argv[i];
        }
    }

    if (!(slicing_criterion || remove_unused_only) || !module) {
        errs() << "Usage: llvm-slicer [-debug dd|forward-dd|cd|rd|slice|ptr|postdom]"
                                    " [-remove-unused-only] [-dont-verify] [-c|-crit|-slice] func_call module\n";
        return 1;
    }

    M = llvm::ParseIRFile(module, SMD, context);
    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    // remove unused from module before slicing - it should
    // have no effect
    remove_unused_from_module_rec(M);
    if (remove_unused_only) {
        errs() << "INFO: remove unused parts of module, exiting...\n";
        return 0;
    }

    if (!slice(M, module, slicing_criterion, opts)) {
        errs() << "ERR: Slicing failed\n";
        return 1;
    }

    remove_unused_from_module_rec(M);

    if (should_verify_module) {
        if (!verify_module(M)) {
            errs() << "ERR: Verifying module failed, the IR is not valid\n";
            errs() << "INFO: Saving anyway so that you can check it\n";
        }
    }

    if (!write_module(M, module)) {
        errs() << "Saving sliced module failed\n";
        return 1;
    }

    return 0;
}
