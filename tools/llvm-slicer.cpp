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
#include <llvm/Support/CommandLine.h>

#include <iostream>
#include <fstream>

#include "llvm/LLVMDependenceGraph.h"
#include "llvm/Slicer.h"
#include "Utils.h"

#include "llvm/analysis/old/PointsTo.h"
#include "llvm/analysis/old/ReachingDefs.h"
#include "llvm/analysis/old/DefUse.h"

#include "llvm/analysis/DefUse.h"
#include "llvm/analysis/PointsTo/PointsTo.h"
#include "llvm/analysis/ReachingDefinitions/ReachingDefinitions.h"

#include "analysis/PointsTo/PointsToFlowInsensitive.h"
#include "analysis/PointsTo/PointsToFlowSensitive.h"
#include "analysis/PointsTo/Pointer.h"

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

enum PtaType {
    old, fs, fi
};

llvm::cl::OptionCategory SlicingOpts("Slicer options", "");

llvm::cl::opt<std::string> output("o",
    llvm::cl::desc("Save the output to given file. If not specified,\n"
                   "a .sliced suffix is used with the original module name."),
    llvm::cl::value_desc("filename"), llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<std::string> llvmfile(llvm::cl::Positional, llvm::cl::Required,
    llvm::cl::desc("<input file>"), llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<std::string> slicing_criterion("c", llvm::cl::Required,
    llvm::cl::desc("Slice with respect to the call-sites of a given function\n"
                   "i. e.: '-c foo' or '-c __assert_fail'. Special value is a 'ret'\n"
                   "in which case the slice is taken with respect to the return value\n"
                   "of the main() function\n"), llvm::cl::value_desc("func"),
                   llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<uint64_t> pta_field_sensitivie("pta-field-sensitive",
    llvm::cl::desc("Make PTA field sensitive/insensitive. The offset in a pointer\n"
                   "is cropped to UNKNOWN_OFFSET when it is greater than N bytes.\n"
                   "Default is full field-sensitivity (N = UNKNOWN_OFFSET).\n"),
                   llvm::cl::value_desc("N"), llvm::cl::init(UNKNOWN_OFFSET),
                   llvm::cl::cat(SlicingOpts));

llvm::cl::opt<PtaType> pta("pta",
    llvm::cl::desc("Choose pointer analysis to use:"),
    llvm::cl::values(
        clEnumVal(old , "Old pointer analysis (flow-insensitive, deprecated)"),
        clEnumVal(fi, "Flow-insensitive PTA (default)"),
        clEnumVal(fs, "Flow-sensitive PTA"),
        nullptr),
    llvm::cl::init(fi), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<CD_ALG> CdAlgorithm("cd-alg",
    llvm::cl::desc("Choose control dependencies algorithm to use:"),
    llvm::cl::values(
        clEnumValN(CLASSIC , "classic", "Ferrante's algorithm (default)"),
        clEnumValN(CONTROL_EXPRESSION, "ce", "Control expression based (experimental)"),
        nullptr),
    llvm::cl::init(CLASSIC), llvm::cl::cat(SlicingOpts));


class CommentDBG : public llvm::AssemblyAnnotationWriter
{
    LLVMReachingDefinitions *RD;
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
    CommentDBG(uint32_t o = ANNOTATE_DD,
               LLVMReachingDefinitions *rd = nullptr)
        :RD(rd), opts(o) {}

    virtual void emitFunctionAnnot (const llvm::Function *,
                                    llvm::formatted_raw_ostream &os)
    {
        // dump the slicer's setting to the file
        // for easier comprehension
        static bool didit = false;
        if (!didit) {
            didit = true;
            os << "; -- Generated by llvm-slicer --\n"
               << ";   * slicing criterion: '" << slicing_criterion << "'\n\n";
        }
    }

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

static void annotate(llvm::Module *M, uint32_t opts,
                     LLVMReachingDefinitions *rd = nullptr)
{
    // compose name
    std::string fl(llvmfile);
    fl.replace(fl.end() - 3, fl.end(), "-debug.ll");

    // open stream to write to
    std::ofstream ofs(fl);
    llvm::raw_os_ostream outputstream(ofs);

    errs() << "INFO: Saving IR with annotations to " << fl << "\n";
    llvm::AssemblyAnnotationWriter *annot = new CommentDBG(opts, rd);
    M->print(outputstream, annot);

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
    uint32_t opts = 0;
    LLVMPointerAnalysis *PTA;
    LLVMReachingDefinitions *RD;

    // shared by old and new analyses
    bool sliceGraph(LLVMDependenceGraph &d)
    {
        debug::TimeMeasure tm;
        std::set<LLVMNode *> callsites;

        // verify if the graph is built correctly
        // FIXME - do it optionally (command line argument)
        if (!d.verify()) {
            errs() << "ERR: verifying failed\n";
            return false;
        }

        assert(!slicing_criterion.empty() && "Do not have the slicing criterion");

        // check for slicing criterion here, because
        // we might have built new subgraphs that contain
        // it during points-to analysis
        bool ret = d.getCallSites(slicing_criterion.c_str(), &callsites);
        bool got_slicing_criterion = true;
        if (!ret) {
            if (slicing_criterion == "ret") {
                callsites.insert(d.getExit());
            } else {
                errs() << "Did not find slicing criterion: "
                       << slicing_criterion << "\n";
                got_slicing_criterion = false;
            }
        }

        // we also do not want to remove any assumptions
        // about the code
        // FIXME add command line switch that
        // will add these according to user's will
        // (or extend the slicing criterion to be a list)
        const char *sc[] = {
            "__VERIFIER_assume",
            "klee_assume",
            NULL // termination
        };

        d.getCallSites(sc, &callsites);

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

        // do not slice __VERIFIER_assume at all
        // FIXME: do this optional
        slicer.keepFunctionUntouched("__VERIFIER_assume");

        tm.start();
        for (LLVMNode *start : callsites)
            slid = slicer.mark(start, slid);

        // print debugging llvm IR if user asked for it
        if (opts & ANNOTATE)
            annotate(M, opts, RD);

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

        RD = new analysis::rd::LLVMReachingDefinitions(M, PTA);
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
        d->computeControlDependencies(CdAlgorithm);
        tm.stop();
        tm.report("INFO: Computing control dependencies took");
    }

    // for old slicer -- without creating a pointer analysis
    Slicer(llvm::Module *mod, uint32_t o, bool /* no pta */)
    :M(mod), opts(o), PTA(nullptr), RD(nullptr)
    {
        assert(mod && "Need module");
    }
public:
    Slicer(llvm::Module *mod, uint32_t o)
    :M(mod), opts(o),
     PTA(new LLVMPointerAnalysis(mod, pta_field_sensitivie)), RD(nullptr)
    {
        assert(mod && "Need module");
    }

    ~Slicer()
    {
        delete PTA;
        delete RD;
    }

    bool slice()
    {
        debug::TimeMeasure tm;
        LLVMDependenceGraph d;

        tm.start();

        if (pta == PtaType::fs)
            PTA->run<analysis::pta::PointsToFlowSensitive>();
        else if (pta == PtaType::fi)
            PTA->run<analysis::pta::PointsToFlowInsensitive>();
        else
            assert(0 && "Wrong pointer analysis");

        tm.stop();
        tm.report("INFO: Points-to analysis took");

        d.build(&*M, PTA);

        return sliceGraph(d);
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
        d->computeControlDependencies(CdAlgorithm);
        tm.stop();
        tm.report("INFO: Computing control dependencies took");
    }

public:
    SlicerOld(llvm::Module *mod, uint32_t o = 0)
        :Slicer(mod, o, true /* no new pta */) {}

    bool slice()
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

        return sliceGraph(d);
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

static bool write_module(llvm::Module *M)
{
    // compose name if not given
    std::string fl;
    if (!output.empty()) {
        fl = output;
    } else {
        fl = llvmfile;

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

static int verify_and_write_module(llvm::Module *M)
{
    if (!verify_module(M)) {
        errs() << "ERR: Verifying module failed, the IR is not valid\n";
        errs() << "INFO: Saving anyway so that you can check it\n";
        return 1;
    }

    if (!write_module(M)) {
        errs() << "Saving sliced module failed\n";
        return 1;
    }

    // exit code
    return 0;
}

static int save_module(llvm::Module *M,
                       bool should_verify_module = true)
{
    if (should_verify_module)
        return verify_and_write_module(M);
    else
        return write_module(M);
}

static uint32_t parseAnnotationOpt(const std::string& opt)
{
    if (opt.empty())
        return 0;

    uint32_t opts = 0;
    size_t pos = 0;
    while (true) {
        if (opt.compare(pos, 2 /* len */, "dd") == 0)
            opts |= ANNOTATE_DD;
        else if (opt.compare(pos, 2, "cd") == 0)
            opts |= ANNOTATE_CD;
        else if (opt.compare(pos, 2, "rd") == 0)
            opts |= ANNOTATE_RD;
        else if (opt.compare(pos, 3, "pta") == 0)
            opts |= ANNOTATE_RD;
        else if (opt.compare(pos, 5, "slice") == 0)
            opts |= ANNOTATE_SLICE;

        pos = opt.find(',', pos);
        if (pos == std::string::npos)
            break;
        else
            ++pos;
    }

    if (opts != 0)
        opts |= ANNOTATE;

    return opts;
}

int main(int argc, char *argv[])
{
    llvm::sys::PrintStackTraceOnErrorSignal();
    llvm::PrettyStackTraceProgram X(argc, argv);

    llvm::Module *M = nullptr;
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;

    llvm::cl::opt<bool> should_verify_module("dont-verify",
        llvm::cl::desc("Verify sliced module (default=true)."),
        llvm::cl::init(true), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> remove_unused_only("remove-unused-only",
        llvm::cl::desc("Only remove unused parts of module (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> statistics("statistics",
        llvm::cl::desc("Print statistics about slicing (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<std::string> annot("annotate",
        llvm::cl::desc("Save annotated version of module as a text (.ll).\n"
                       "(dd: data dependencies, cd:control dependencies,\n"
                       "rd: reaching definitions, pta: points-to information,\n"
                       "slice: comment out what is going to be sliced away, etc.)\n"
                       "for more options, use comma separated list"),
        llvm::cl::value_desc("val1,val2,..."), llvm::cl::init(""),
        llvm::cl::cat(SlicingOpts));

    // hide all options except ours options
    llvm::cl::HideUnrelatedOptions(SlicingOpts);
    llvm::cl::SetVersionPrinter([](){ printf("%s\n", GIT_VERSION); });
    llvm::cl::ParseCommandLineOptions(argc, argv);

    uint32_t opts = parseAnnotationOpt(annot);

#if (LLVM_VERSION_MINOR < 5)
    M = llvm::ParseIRFile(llvmfile, SMD, context);
#else
    auto _M = llvm::parseIRFile(llvmfile, SMD, context);
    // _M is unique pointer, we need to get Module *
    M = _M.get();
#endif

    if (!M) {
        llvm::errs() << "Failed parsing '" << llvmfile << "' file:\n";
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

        return save_module(M, should_verify_module);
    }

    /// ---------------
    // slice the code
    /// ---------------
    if (pta == PtaType::old) {
        SlicerOld slicer(M, opts);
        if (!slicer.slice()) {
            errs() << "ERR: Slicing failed\n";
            return 1;
        }
    } else {
        // FIXME: do one parent class and use overriding
        Slicer slicer(M, opts);
        if (!slicer.slice()) {
            errs() << "ERR: Slicing failed\n";
            return 1;
        }
    }

    // remove unused from module again, since slicing
    // could and probably did make some other parts unused
    remove_unused_from_module_rec(M);

    // fix linkage of declared functions (if needs to be fixed)
    make_declarations_external(M);

    if (statistics)
        print_statistics(M, "Statistics after ");

    return save_module(M, should_verify_module);
}
