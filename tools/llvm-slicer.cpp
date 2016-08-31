#include <assert.h>
#include <cstdlib>
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
#include <llvm/Support/Signals.h>
#include <llvm/Support/PrettyStackTrace.h>

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
"                   [-remove-unused-only] [-dont-verify]\n"
"                   [-pta fs|fi|old][-pta-field-sensitive N]\n"
"                   [-rd-field-insensitive][-rd-max-set-size N]\n"
"                   [-c|-crit|-slice func_call] [-cd-alg classic|ce]\n"
"                   [-o file] module\n"
"\n"
"-debug                  Save annotated version of module as a text (.ll).\n"
"                            (dd: data dependencies, cd:control dependencies,\n"
"                             rd: reaching definitions, ptr: points-to information,\n"
"                             slice: comment out what is going to be sliced away, etc.)\n"
"                        This option can be used more times, i. e. '-debug dd -debug slice'\n"
"-remove-unused-only     Remove unused parts of module, but do not slice\n"
"-dont-verify            Don't verify wheter the module is valid after slicing\n"
"-pta                    What points-to analysis use:\n"
"                            fs - flow-sensitive\n"
"                            fi - flow-insensitive\n"
"                            old - old flow-insensitive, default\n"
"-pta-field-sensitive N  Make PTA field sensitive/insensitive. The offset in a pointer\n"
"                        is cropped to UNKNOWN_OFFSET when it is greater than N bytes.\n"
"                        Default is full field-sensitivity (N = UNKNOWN_OFFSET).\n"
"-rd-field-insensitive   Make reaching definitions analysis kind of field insensitive\n."
"-rd-max-set-size N      Crop set of reaching definitions to unknown when it is greater than N\n."
" -c                     Slice with respect to the call-sites of func_call\n"
"                        i. e.: '-c foo' or '-c __assert_fail'. Special value is 'ret'\n"
"                        in which case the slice is taken with respect to return value\n"
"                        of main procedure\n"
" -cd-alg                Which algorithm for computing control dependencies to use.\n"
"                        'classical' is the original Ferrante's algorithm and ce\n"
"                        is our algorithm using so called control expression (default)\n"
" -o                     Save the output to given file. If not specified, a .sliced suffix\n"
"                        is used with the original module name\n"
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

    void printPointer(const analysis::pta::Pointer& ptr,
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
            if (ds.target->isUnknown())
                os << "unknown";
            else
                printValue(val, os);

            if (ds.offset.isUnknown())
                os << " bytes |UNKNOWN";
            else
                os << " bytes |" << *ds.offset;

            if (ds.len.isUnknown())
                os << " - UNKNOWN|";
            else
                os << " - " <<  *ds.offset + *ds.len- 1 << "|";
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
                        for (auto nd : it.second) {
                            printDefSite(it.first, os, "RD: ");
                            os << " @ ";
                            if (nd->isUnknown())
                                os << " UNKNOWN\n";
                            else
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
            // don't dump params when we use new analyses (RD is not null)
            // because there we don't add definitions with new analyses
            if (params && !RD) {
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
                os << "  ; DD: ";

                if (d->hasName())
                    os << d->getName();
                else
                    os << *d;

                os << "(" << d << ")\n";
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
                os << "  ; rCD: ";

                if (d->hasName())
                    os << d->getName() << "\n";
                else
                    os << *d << "\n";
            }
        }

        if (opts & ANNOTATE_PTR) {
            // FIXME: use the PTA from Slicer class
            LLVMPointerAnalysis *PTA = node->getDG()->getPTA();
            if (PTA) { // we used the new analyses
                llvm::Type *Ty = node->getKey()->getType();
                if (Ty->isPointerTy() || Ty->isIntegerTy()) {
                    analysis::pta::PSNode *ps = PTA->getPointsTo(node->getKey());
                    if (ps) {
                        for (const analysis::pta::Pointer& ptr : ps->pointsTo)
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
                if (opts & (ANNOTATE_POSTDOM | ANNOTATE_CD))
                    os << "  ; BB: " << BB << "\n";

                if (opts & ANNOTATE_POSTDOM) {
                    for (LLVMBBlock *p : BB->getPostDomFrontiers())
                        os << "  ; PDF: " << p << "\n";

                    LLVMBBlock *P = BB->getIPostDom();
                    if (P && P->getKey())
                        os << "  ; iPD: " << P << "\n";
                }

                if (opts & ANNOTATE_CD) {
                    for (LLVMBBlock *p : BB->controlDependence())
                        os << "  ; CD: " << p << "\n";
                }
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
    CD_ALG cd_alg;
    LLVMPointerAnalysis *PTA;
    LLVMReachingDefinitions *RD;
    uint32_t rd_max_set_size;
    bool rd_field_insensitive;

    // for SlicerOld
    Slicer(llvm::Module *mod, const char *modnm,
           uint32_t o, CD_ALG cda = CLASSIC,
           uint64_t field_sens = UNKNOWN_OFFSET,
           uint32_t rd_max_set_sz = ~((uint32_t) 0),
           bool rd_field_insens = false)
    :M(mod), module_name(modnm), opts(o), cd_alg(cda),
     PTA(new LLVMPointerAnalysis(mod, field_sens)), RD(nullptr),
     rd_max_set_size(rd_max_set_sz), rd_field_insensitive(rd_field_insens)
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

        RD = new analysis::rd::LLVMReachingDefinitions(M, PTA,
                                                       rd_field_insensitive,
                                                       rd_max_set_size);
        tm.start();
        RD->run();
        tm.stop();
        tm.report("INFO: Reaching defs analysis took");

        LLVMDefUseAnalysis DUA(d, RD, PTA);
        tm.start();
        DUA.run(); // add def-use edges according that
        tm.stop();
        tm.report("INFO: Adding Def-Use edges took");

        tm.start();
        // add post-dominator frontiers
        d->computeControlDependencies(cd_alg);
        tm.stop();
        tm.report("INFO: Computing control dependencies took");
    }

public:

    // FIXME: make pts enum, not a string
    Slicer(llvm::Module *mod, const char *modnm,
           uint32_t o, PtaType pt, CD_ALG cda = CLASSIC,
           uint64_t field_sens = UNKNOWN_OFFSET,
           uint32_t rd_max_set_size = ~((uint32_t) 0),
           bool rd_field_insens = false)
    :Slicer(mod, modnm, o, cda, field_sens, rd_max_set_size, rd_field_insens)
    {
        assert((pt == PTA_FI || pt == PTA_FS) && "Invalid PTA");
        this->pta = pt; // cannot do it in mem-initialization,
                         // since we delegate the constructor
    }

    ~Slicer()
    {
        delete PTA;
        delete RD;
    }

    bool slice(const char *slicing_criterion)
    {
        debug::TimeMeasure tm;
        LLVMDependenceGraph d;

        tm.start();

        if (pta == PTA_FS)
            PTA->run<analysis::pta::PointsToFlowSensitive>();
        else if (pta == PTA_FI)
            PTA->run<analysis::pta::PointsToFlowInsensitive>();
        else
            assert(0 && "Should not be reached");

        tm.stop();
        tm.report("INFO: Points-to analysis took");

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
        d->computeControlDependencies(cd_alg);
        tm.stop();
        tm.report("INFO: Computing control dependencies took");
    }

public:
    SlicerOld(llvm::Module *mod, const char *modnm,
              uint32_t o = 0, CD_ALG cda = CLASSIC)
        :Slicer(mod, modnm, o, cda) {}

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

// after we slice the LLVM, we somethimes have troubles
// with function declarations:
//
//   Global is external, but doesn't have external or dllimport or weak linkage!
//   i32 (%struct.usbnet*)* @always_connected
//   invalid linkage type for function declaration
//
// This function makes the declarations external
static void make_declarations_external(llvm::Module *M)
{
    using namespace llvm;

    // when erasing while iterating the slicer crashes
    // so set the to be erased values into container
    // and then erase them
    for (auto I = M->begin(), E = M->end(); I != E; ++I) {
        Function *func = &*I;
        if (func->size() == 0) {
            // this will make sure that the linkage has right type
            func->deleteBody();
        }
    }
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

static bool write_module(llvm::Module *M, const char *module_name,
                         const char *output)
{
    // compose name if not given
    std::string fl;
    if (output) {
        fl = std::string(output);
    } else {
        fl = std::string(module_name);

        if (fl.size() > 2) {
            if (fl.compare(fl.size() - 2, 2, ".o") == 0)
                fl.replace(fl.end() - 2, fl.end(), ".sliced");
            else if (fl.compare(fl.size() - 3, 3, ".bc") == 0)
                fl.replace(fl.end() - 3, fl.end(), ".sliced");
            else
                fl += ".sliced";
        } else {
            fl += ".sliced";
        }
    }

    // open stream to write to
    std::ofstream ofs(fl);
    llvm::raw_os_ostream ostream(ofs);

    // write the module
    errs() << "INFO: saving sliced module to: " << fl.c_str() << "\n";
    llvm::WriteBitcodeToFile(M, ostream);

    return true;
}

static int verify_and_write_module(llvm::Module *M, const char *module,
                                   const char *output) {
    if (!verify_module(M)) {
        errs() << "ERR: Verifying module failed, the IR is not valid\n";
        errs() << "INFO: Saving anyway so that you can check it\n";
        return 1;
    }

    if (!write_module(M, module, output)) {
        errs() << "Saving sliced module failed\n";
        return 1;
    }

    // exit code
    return 0;
}

static int save_module(llvm::Module *M, const char *module,
                       const char *output, bool should_verify_module = true)
{
    if (should_verify_module)
        return verify_and_write_module(M, module, output);
    else
        return write_module(M, module, output);
}

int main(int argc, char *argv[])
{
    llvm::sys::PrintStackTraceOnErrorSignal();
    llvm::PrettyStackTraceProgram X(argc, argv);

    llvm::Module *M;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;

    bool should_verify_module = true;
    bool remove_unused_only = false;
    bool statistics = false;
    const char *slicing_criterion = nullptr;
    const char *module = nullptr;
    const char *output = nullptr;
    uint32_t opts = 0;
    PtaType pta = PTA_FI;
    CD_ALG cd_alg = CLASSIC;
    uint64_t field_senitivity = UNKNOWN_OFFSET;
    bool rd_field_insensitive = false;
    uint32_t rd_max_set_size = ~((uint32_t) 0);

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
        } else if (strcmp(argv[i], "-cd-alg") == 0) {
            const char *arg = argv[++i];
            if (strcmp(arg, "classic") == 0)
                cd_alg = CLASSIC;
            else if (strcmp(arg, "ce") == 0)
                cd_alg = CONTROL_EXPRESSION;
            else {
                errs() << "Invalid control dependencies algorithm, try: classic, ce\n";
                abort();
            }
        } else if (strcmp(argv[i], "-o") == 0) {
            output = strdup(argv[++i]);
            if (!output) {
                errs() << "Out of memory\n";
                abort();
            }
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
        } else if (strcmp(argv[i], "-pta-field-sensitive") == 0) {
            // FIXME: check the converted value
            field_senitivity = (uint64_t) atoll(argv[i+1]);
        } else if (strcmp(argv[i], "-rd-max-set-size") == 0) {
            rd_max_set_size = (uint64_t) atoll(argv[i + 1]);
            if (rd_max_set_size == 0) {
                llvm::errs() << "Invalid -rd-max-set-size argument\n";
                abort();
            }
        } else if (strcmp(argv[i], "-rd-field-insensitive") == 0) {
            rd_field_insensitive = true;

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
    M = _M.get();
#endif

    if (!M) {
        llvm::errs() << "Failed parsing '" << module << "' file:\n";
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

        return save_module(M, module, output, should_verify_module);
    }

    /// ---------------
    // slice the code
    /// ---------------
    if (pta == PTA_OLD) {
        SlicerOld slicer(M, module, opts, cd_alg);
        if (!slicer.slice(slicing_criterion)) {
            errs() << "ERR: Slicing failed\n";
            return 1;
        }
    } else if (pta == PTA_FI || pta == PTA_FS) {
        // FIXME: do one parent class and use overriding
        Slicer slicer(M, module, opts, pta, cd_alg,
                      field_senitivity, rd_max_set_size, rd_field_insensitive);
        if (!slicer.slice(slicing_criterion)) {
            errs() << "ERR: Slicing failed\n";
            return 1;
        }
    } else
        assert(0 && "Should not be reached");

    // remove unused from module again, since slicing
    // could and probably did make some other parts unused
    remove_unused_from_module_rec(M);

    // fix linkage of declared functions (if needs to be fixed)
    make_declarations_external(M);

    if (statistics)
        print_statistics(M, "Statistics after ");

    return save_module(M, module, output, should_verify_module);
    // FIXME: we leak the output string
}
