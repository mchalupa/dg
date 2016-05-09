#include <assert.h>
#include <cstdio>
#include <cstring>

#include <set>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include "../git-version.h"
#include <llvm/Config/llvm-config.h>


#if (LLVM_VERSION_MAJOR != 3)
#error "Unsupported version of LLVM"
#endif

#if LLVM_VERSION_MINOR < 5
 #include <llvm/Assembly/AssemblyAnnotationWriter.h>
 #include <llvm/Analysis/Verifier.h>
#else // >= 3.5
 #include <llvm/IR/AssemblyAnnotationWriter.h>
 #include <llvm/IR/Verifier.h>
#endif

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
#include "llvm/Slicer.h"
#include "Utils.h"

#include "llvm/analysis/old/PointsTo.h"
#include "llvm/analysis/old/ReachingDefs.h"
#include "llvm/analysis/old/DefUse.h"

#include "llvm/analysis/DefUse.h"
#include "llvm/analysis/PointsTo.h"
#include "llvm/analysis/ReachingDefinitions.h"

#include "analysis/PointsTo/PointsToFlowInsensitive.h"
#include "analysis/PointsTo/PointsToFlowSensitive.h"
#include "analysis/PointsTo/Pointer.h"

using namespace dg;
using llvm::errs;

const char *usage =
"Usage: llvm-slicer [-debug dd|forward-dd|cd|rd|slice|ptr|postdom]\n"
"                   [-remove-unused-only] [-dont-verify] [-pts fs|fi|old]\n"
"                   [-c|-crit|-slice func_call] module\n"
"\n"
"-debug               Save annotated version of module as a text (.ll).\n"
"                         (dd: data dependencies, cd:control dependencies,\n"
"                          rd: reaching definitions, ptr: points-to information,\n"
"                          slice: comment out what is going to be sliced away, etc.)\n"
"                     This option can be used more times, i. e. '-debug dd -debug slice'\n"
"-remove-unused-only  Remove unused parts of module, but do not slice\n"
"-dont-verify         Don't verify wheter the module is valid after slicing\n"
"-pts                 What points-to analysis use:\n"
"                         fs - flow-sensitive\n"
"                         fi - flow-insensitive\n"
"                         old - old flow-insensitive, default\n"
" -c                  Slice with respect to the call-sites of func_call\n"
"                     i. e.: '-c foo' or '-c __assert_fail'. Special value is 'ret'\n"
"                     in which case the slice is taken with respect to return value\n"
"                     of main procedure\n"
"\n"
"'module' is the LLVM bitcode files to be sliced. It must contain 'main' function\n";

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

enum PtaType {
    PTA_OLD = 0,
    PTA_FS,
    PTA_FI
};

class CommentDBG : public llvm::AssemblyAnnotationWriter
{
    LLVMReachingDefinitions *RD;
    LLVMDependenceGraph *dg;
    uint32_t opts;

    void printValue(const llvm::Value *val,
                    llvm::formatted_raw_ostream& os,
                    bool nl = false)
    {
        if (val->hasName())
            os << val->getName().data();
        else
            os << *val;

        if (nl)
            os << "\n";
    }

    void printPointer(const analysis::pss::Pointer& ptr,
                      llvm::formatted_raw_ostream& os,
                      const char *prefix = "PTR: ", bool nl = true)
    {
        os << "  ; ";
        if (prefix)
            os << prefix;

        if (!ptr.isUnknown()) {
            if (ptr.isNull())
                os << "null";
            else {
                const llvm::Value *val
                    = ptr.target->getUserData<llvm::Value>();
                printValue(val, os);
            }

            os << " + ";
            if (ptr.offset.isUnknown())
                os << "UNKNOWN";
            else
                os << *ptr.offset;
        } else
            os << "unknown";

        if (nl)
            os << "\n";
    }

    void printPointer(const analysis::Pointer& ptr,
                      llvm::formatted_raw_ostream& os,
                      const char *prefix = "PTR: ", bool nl = true)
    {
        os << "  ; ";
        if (prefix)
            os << prefix;

        if (ptr.isKnown()) {
            const llvm::Value *val = ptr.obj->node->getKey();
            printValue(val, os);

            if (ptr.offset.isUnknown())
                os << " + UNKNOWN";
            else
                os << " + " << *ptr.offset;
        } else
            os << "unknown";

        if (nl)
            os << "\n";
    }

    void printDefSite(const analysis::rd::DefSite& ds,
                      llvm::formatted_raw_ostream& os,
                      const char *prefix = nullptr, bool nl = false)
    {
        os << "  ; ";
        if (prefix)
            os << prefix;

        if (ds.target) {
            const llvm::Value *val = ds.target->getUserData<llvm::Value>();
            printValue(val, os);

            if (ds.offset.isUnknown())
                os << " bytes |UNKNOWN";
            else
                os << " bytes |" << *ds.offset;

            if (ds.len.isUnknown())
                os << " - UNKNOWN|";
            else
                os << " - " << *ds.len - 1 << "|";
        } else
            os << "target is null!";

        if (nl)
            os << "\n";

    }

    void emitNodeAnnotations(LLVMNode *node, llvm::formatted_raw_ostream& os)
    {
        if (opts & ANNOTATE_RD) {
            if (RD) {
                analysis::rd::RDNode *rd = RD->getMapping(node->getKey());
                if (!rd) {
                    os << "  ; RD: no mapping\n";
                } else {
                    auto defs = rd->getReachingDefinitions();
                    for (auto it : defs) {
                        printDefSite(it.first, os, "RD: ");
                        for (auto nd : it.second) {
                            os << " @ ";
                            printValue(nd->getUserData<llvm::Value>(), os, true);
                        }
                    }
                }
            } else {
                analysis::DefMap *df = node->getData<analysis::DefMap>();
                if (df) {
                    for (auto it : *df) {
                        for (LLVMNode *d : it.second) {
                            printPointer(it.first, os, "RD: ", false);
                            os << " @ " << *d->getKey() << "(" << d << ")\n";
                        }
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
            // FIXME: use the PTA from Slicer class
            LLVMPointsToAnalysis *PTA = node->getDG()->getPTA();
            if (PTA) { // we used the new analyses
                llvm::Type *Ty = node->getKey()->getType();
                if (Ty->isPointerTy() || Ty->isIntegerTy()) {
                    analysis::pss::PSSNode *ps = PTA->getPointsTo(node->getKey());
                    if (ps) {
                        for (const analysis::pss::Pointer& ptr : ps->pointsTo)
                            printPointer(ptr, os);
                    }
                }
            } else {
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
        }

        if (opts & ANNOTATE_SLICE)
            if (node->getSlice() == 0)
                os << "  ; x ";
    }

public:
    CommentDBG(LLVMDependenceGraph *dg, uint32_t o = ANNOTATE_DD,
               LLVMReachingDefinitions *rd = nullptr)
        :dg(dg), RD(rd), opts(o) {}

    virtual void emitInstructionAnnot(const llvm::Instruction *I,
                                      llvm::formatted_raw_ostream& os)
    {
        if (opts == 0)
            return;

        LLVMNode *node = nullptr;
        for (auto it : getConstructedFunctions()) {
            LLVMDependenceGraph *sub = it.second;
            node = sub->getNode(const_cast<llvm::Instruction *>(I));
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
            LLVMBBlock *BB = cb[const_cast<llvm::BasicBlock *>(B)];
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
                     LLVMDependenceGraph *d, uint32_t opts,
                     LLVMReachingDefinitions *rd = nullptr)
{
    // compose name
    std::string fl(module_name);
    fl.replace(fl.end() - 3, fl.end(), "-debug.ll");

    // open stream to write to
    std::ofstream ofs(fl);
    llvm::raw_os_ostream output(ofs);

    errs() << "INFO: Saving IR with annotations to " << fl << "\n";
    llvm::AssemblyAnnotationWriter *annot = new CommentDBG(d, opts, rd);
    M->print(output, annot);

    delete annot;
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

class Slicer {
protected:
    llvm::Module *M;
    const char *module_name;
    uint32_t opts = 0;
    PtaType pta;
    LLVMPointsToAnalysis *PTA;
    LLVMReachingDefinitions *RD;

    // for SlicerOld
    Slicer(llvm::Module *mod, const char *modnm, uint32_t o)
    :M(mod), module_name(modnm), opts(o), PTA(nullptr), RD(nullptr)
    {
        assert(mod && "Need module");
        assert(modnm && "Need name of module");
    }

    // shared by old and new analyses
    bool sliceGraph(LLVMDependenceGraph &d, const char *slicing_criterion)
    {
        debug::TimeMeasure tm;
        std::set<LLVMNode *> callsites;

        // verify if the graph is built correctly
        // FIXME - do it optionally (command line argument)
        if (!d.verify()) {
            errs() << "ERR: verifying failed\n";
            return false;
        }

        // FIXME add command line switch -svcomp and
        // do this only with -svcomp switch
        const char *sc[] = {
            slicing_criterion,
            "klee_assume",
            NULL // termination
        };

        // check for slicing criterion here, because
        // we might have built new subgraphs that contain
        // it during points-to analysis
        bool ret = d.getCallSites(sc, &callsites);
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
            computeEdges(&d);

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
            annotate(M, module_name, &d, opts, RD);

        slicer.slice(&d, nullptr, slid);

        tm.stop();
        tm.report("INFO: Slicing dependence graph took");

        analysis::SlicerStatistics& st = slicer.getStatistics();
        errs() << "INFO: Sliced away " << st.nodesRemoved
               << " from " << st.nodesTotal << " nodes in DG\n";

        return true;
    }

    virtual void computeEdges(LLVMDependenceGraph *d)
    {
        debug::TimeMeasure tm;
        assert(PTA && "BUG: No PTA");

        LLVMDefUseAnalysis DUA(d, RD, PTA);
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

public:

    // FIXME: make pts enum, not a string
    Slicer(llvm::Module *mod, const char *modnm, uint32_t o, PtaType pt)
    :Slicer(mod, modnm, o)
    {
        assert((pt == PTA_FI || pt == PTA_FS) && "Invalid PTA");
        this->pta = pt; // cannot do it in mem-initialization,
                         // since we delegate the constructor
    }

    bool slice(const char *slicing_criterion)
    {
        debug::TimeMeasure tm;
        LLVMDependenceGraph d;

        if (pta == PTA_FS)
            PTA = new LLVMPointsToAnalysisImpl<analysis::pss::PointsToFlowSensitive>(M);
        else if (pta == PTA_FI)
            PTA = new LLVMPointsToAnalysisImpl<analysis::pss::PointsToFlowInsensitive>(M);
        else
            assert(0 && "Should not be reached");

        tm.start();
        PTA->run();
        tm.stop();
        tm.report("INFO: Points-to analysis took");

        RD = new analysis::rd::LLVMReachingDefinitions(M, PTA);
        tm.start();
        RD->run();
        tm.stop();
        tm.report("INFO: Reaching defs analysis took");

        d.build(&*M, PTA);

        return sliceGraph(d, slicing_criterion);
        // FIXME: we're leaking PTA & RD
    }
};

class SlicerOld : public Slicer
{
    virtual void computeEdges(LLVMDependenceGraph *d)
    {
        debug::TimeMeasure tm;
        assert(PTA == nullptr);

        analysis::LLVMReachingDefsAnalysis RDA(d);
        tm.start();
        RDA.run();  // compute reaching definitions
        tm.stop();
        tm.report("INFO: Reaching defs analysis [old] took");

        analysis::old::LLVMDefUseAnalysis DUA(d);
        tm.start();
        DUA.run(); // add def-use edges according that
        tm.stop();
        tm.report("INFO: Adding Def-Use edges [old] took");

        tm.start();
        // add post-dominator frontiers
        d->computePostDominators(true);
        tm.stop();
        tm.report("INFO: Computing post-dominator frontiers took");
    }

public:
    SlicerOld(llvm::Module *mod, const char *modnm, uint32_t o = 0)
        :Slicer(mod, module_name, o) {}

    bool slice(const char *slicing_criterion)
    {
        debug::TimeMeasure tm;
        LLVMDependenceGraph d;

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
        tm.report("INFO: Points-to analysis [old] took");

        return sliceGraph(d, slicing_criterion);
    }
};

static void print_statistics(llvm::Module *M, const char *prefix = nullptr)
{
    using namespace llvm;
    uint64_t inum, bnum, fnum, gnum;
    inum = bnum = fnum = gnum = 0;

    for (auto I = M->begin(), E = M->end(); I != E; ++I) {
        // don't count in declarations
        if (I->size() == 0)
            continue;

        ++fnum;

        for (const BasicBlock& B : *I) {
            ++bnum;
            inum += B.size();
        }
    }

    for (auto I = M->global_begin(), E = M->global_end(); I != E; ++I)
        ++gnum;

    if (prefix)
        errs() << prefix;

    errs() << "Globals/Functions/Blocks/Instr.: "
           << gnum << " " << fnum << " " << bnum << " " << inum << "\n";
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

#if (LLVM_VERSION_MINOR >= 5)
    return !llvm::verifyModule(*M, &llvm::errs());
#else
    return !llvm::verifyModule(*M, llvm::PrintMessageAction);
#endif
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

static int verify_and_write_module(llvm::Module *M, const char *module) {
    if (!verify_module(M)) {
        errs() << "ERR: Verifying module failed, the IR is not valid\n";
        errs() << "INFO: Saving anyway so that you can check it\n";
        return 1;
    }

    if (!write_module(M, module)) {
        errs() << "Saving sliced module failed\n";
        return 1;
    }
}

static int save_module(llvm::Module *M, const char *module,
                       bool should_verify_module = true)
{
    if (should_verify_module)
        return verify_and_write_module(M, module);
    else
        return write_module(M, module);
}

int main(int argc, char *argv[])
{
    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;

    bool should_verify_module = true;
    bool remove_unused_only = false;
    bool statistics = false;
    const char *slicing_criterion = nullptr;
    const char *module = nullptr;
    uint32_t opts = 0;
    PtaType pta = PTA_OLD;

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
        } else if (strcmp(argv[i], "-statistics") == 0) {
            statistics = true;
        } else if (strcmp(argv[i], "-pta") == 0) {
            if (strcmp(argv[i+1], "fi") == 0)
                pta = PTA_FI;
            else if (strcmp(argv[i+1], "fs") == 0)
                pta = PTA_FS;
            else if (strcmp(argv[i+1], "old") == 0)
                pta = PTA_OLD;
            else {
                errs() << "Invalid points-to analysis, try: fi, fs, old (or none)\n";
                return 1;
            }

            ++i;
        } else if (strcmp(argv[i], "-dont-verify") == 0) {
            // for debugging
            should_verify_module = false;
        } else {
            module = argv[i];
        }
    }

    if (!(slicing_criterion || remove_unused_only) || !module) {
        errs() << usage;
        return 1;
    }

#if (LLVM_VERSION_MINOR < 5)
    M = llvm::ParseIRFile(module, SMD, context);
#else
    auto _M = llvm::parseIRFile(module, SMD, context);
    // _M is unique pointer, we need to get Module *
    M = &*_M;
#endif

    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    if (statistics)
        print_statistics(M, "Statistics before ");

    // remove unused from module, we don't need that
    remove_unused_from_module_rec(M);

    if (remove_unused_only) {
        errs() << "INFO: removed unused parts of module, exiting...\n";
        if (statistics)
            print_statistics(M, "Statistics after ");

        return save_module(M, module, should_verify_module);
    }

    /// ---------------
    // slice the code
    /// ---------------
    if (pta == PTA_OLD) {
        SlicerOld slicer(M, module, opts);
        if (!slicer.slice(slicing_criterion)) {
            errs() << "ERR: Slicing failed\n";
            return 1;
        }
    } else if (pta == PTA_FI || pta == PTA_FS) {
        // FIXME: do one parent class and use overriding
        Slicer slicer(M, module, opts, pta);
        if (!slicer.slice(slicing_criterion)) {
            errs() << "ERR: Slicing failed\n";
            return 1;
        }
    } else
        assert(0 && "Should not be reached");

    // remove unused from module again, since slicing
    // could and probably did make some other parts unused
    remove_unused_from_module_rec(M);

    if (statistics)
        print_statistics(M, "Statistics after ");

    return save_module(M, module, should_verify_module);
}
