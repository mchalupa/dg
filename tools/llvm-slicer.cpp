#include <set>
#include <string>

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/Config/llvm-config.h>

#if (LLVM_VERSION_MAJOR < 3)
#error "Unsupported version of LLVM"
#endif

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/IntrinsicInst.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include <iostream>
#include <fstream>

#include "llvm/LLVMDependenceGraph.h"
#include "llvm/LLVMDependenceGraphBuilder.h"
#include "llvm/LLVMDGAssemblyAnnotationWriter.h"
#include "llvm/Slicer.h"
#include "llvm/LLVMDG2Dot.h"
#include "TimeMeasure.h"

#include "git-version.h"

using namespace dg;

using llvm::errs;
using llvmdg::LLVMDependenceGraphBuilder;
using dg::analysis::LLVMPointerAnalysisOptions;
using dg::analysis::LLVMReachingDefinitionsAnalysisOptions;

using AnnotationOptsT
        = dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT;

// mapping of AllocaInst to the names of C variables
std::map<const llvm::Value *, std::string> valuesToVariables;

llvm::cl::OptionCategory SlicingOpts("Slicer options", "");

llvm::cl::opt<std::string> output("o",
    llvm::cl::desc("Save the output to given file. If not specified,\n"
                   "a .sliced suffix is used with the original module name."),
    llvm::cl::value_desc("filename"), llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<std::string> llvmfile(llvm::cl::Positional, llvm::cl::Required,
    llvm::cl::desc("<input file>"), llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<std::string> slicing_criteria("c", llvm::cl::Required,
    llvm::cl::desc("Slice with respect to the call-sites of a given function\n"
                   "i. e.: '-c foo' or '-c __assert_fail'. Special value is a 'ret'\n"
                   "in which case the slice is taken with respect to the return value\n"
                   "of the main function. Further, you can specify the criterion as\n"
                   "l:v where l is the line in the original code and v is the variable.\n"
                   "l must be empty when v is a global variable. For local variables,\n"
                   "the variable v must be used on the line l.\n"
                   "You can use comma-separated list of more slicing criteria,\n"
                   "e.g. -c foo,5:x,:glob\n"), llvm::cl::value_desc("crit"),
                   llvm::cl::init(""), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> remove_slicing_criteria("remove-slicing-criteria",
    llvm::cl::desc("By default, slicer keeps also calls to the slicing criteria\n"
                   "in the sliced program. This switch makes slicer to remove\n"
                   "also the calls (i.e. behave like Weisser's algorithm)"),
                   llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<uint64_t> pta_field_sensitivie("pta-field-sensitive",
    llvm::cl::desc("Make PTA field sensitive/insensitive. The offset in a pointer\n"
                   "is cropped to Offset::UNKNOWN when it is greater than N bytes.\n"
                   "Default is full field-sensitivity (N = Offset::UNKNOWN).\n"),
                   llvm::cl::value_desc("N"), llvm::cl::init(Offset::UNKNOWN),
                   llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> rd_strong_update_unknown("rd-strong-update-unknown",
    llvm::cl::desc("Let reaching defintions analysis do strong updates on memory defined\n"
                   "with uknown offset in the case, that new definition overwrites\n"
                   "the whole memory. May be unsound for out-of-bound access\n"),
                   llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> undefined_are_pure("undefined-are-pure",
    llvm::cl::desc("Assume that undefined functions have no side-effects\n"),
                   llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<std::string> entry_func("entry",
    llvm::cl::desc("Entry function of the program\n"),
                   llvm::cl::init("main"), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> forward_slice("forward",
    llvm::cl::desc("Perform forward slicing\n"),
                   llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<LLVMPointerAnalysisOptions::AnalysisType> ptaType("pta",
    llvm::cl::desc("Choose pointer analysis to use:"),
    llvm::cl::values(
        clEnumValN(LLVMPointerAnalysisOptions::AnalysisType::fi, "fi", "Flow-insensitive PTA (default)"),
        clEnumValN(LLVMPointerAnalysisOptions::AnalysisType::fs, "fs", "Flow-sensitive PTA"),
        clEnumValN(LLVMPointerAnalysisOptions::AnalysisType::inv, "inv", "PTA with invalidate nodes")
#if LLVM_VERSION_MAJOR < 4
        , nullptr
#endif
        ),
    llvm::cl::init(LLVMPointerAnalysisOptions::AnalysisType::fi), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<LLVMReachingDefinitionsAnalysisOptions::AnalysisType> rdaType("rda",
    llvm::cl::desc("Choose reaching definitions analysis to use:"),
    llvm::cl::values(
        clEnumValN(LLVMReachingDefinitionsAnalysisOptions::AnalysisType::dense, "dense", "Dense RDA (default)"),
        clEnumValN(LLVMReachingDefinitionsAnalysisOptions::AnalysisType::ss,    "ss",    "Semi-sparse RDA")
#if LLVM_VERSION_MAJOR < 4
        , nullptr
#endif
        ),
    llvm::cl::init(LLVMReachingDefinitionsAnalysisOptions::AnalysisType::dense), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<CD_ALG> cdAlgorithm("cd-alg",
    llvm::cl::desc("Choose control dependencies algorithm to use:"),
    llvm::cl::values(
        clEnumValN(CD_ALG::CLASSIC , "classic", "Ferrante's algorithm (default)"),
        clEnumValN(CD_ALG::CONTROL_EXPRESSION, "ce", "Control expression based (experimental)")
#if LLVM_VERSION_MAJOR < 4
        , nullptr
#endif
         ),
    llvm::cl::init(CD_ALG::CLASSIC), llvm::cl::cat(SlicingOpts));


static void annotate(llvm::Module *M, AnnotationOptsT opts,
                     LLVMPointerAnalysis *PTA,
                     LLVMReachingDefinitions *RD,
                     const std::set<LLVMNode *> *criteria)
{
    // compose name
    std::string fl(llvmfile);
    fl.replace(fl.end() - 3, fl.end(), "-debug.ll");

    // open stream to write to
    std::ofstream ofs(fl);
    llvm::raw_os_ostream outputstream(ofs);

    std::string module_comment =
    "; -- Generated by llvm-slicer --\n"
    ";   * slicing criteria: '" + slicing_criteria + "'\n" +
    ";   * forward slice: '" + std::to_string(forward_slice) + "'\n" +
    ";   * remove slicing criteria: '"
         + std::to_string(remove_slicing_criteria) + "'\n" +
    ";   * undefined are pure: '"
         + std::to_string(undefined_are_pure) + "'\n" +
    ";   * pointer analysis: ";
    if (ptaType == LLVMPointerAnalysisOptions::AnalysisType::fi)
        module_comment += "flow-insensitive\n";
    else if (ptaType == LLVMPointerAnalysisOptions::AnalysisType::fs)
        module_comment += "flow-sensitive\n";
    else if (ptaType == LLVMPointerAnalysisOptions::AnalysisType::inv)
        module_comment += "flow-sensitive with invalidate\n";

    module_comment+= ";   * PTA field sensitivity: ";
    if (pta_field_sensitivie == Offset::UNKNOWN)
        module_comment += "full\n\n";
    else
        module_comment += std::to_string(pta_field_sensitivie) + "\n\n";

    errs() << "INFO: Saving IR with annotations to " << fl << "\n";
    auto annot = new dg::debug::LLVMDGAssemblyAnnotationWriter(opts, PTA, RD, criteria);
    annot->emitModuleComment(std::move(module_comment));
    M->print(outputstream, annot);

    delete annot;
}

///
// Create new empty main. If 'call_entry' is set to true, then
// call the entry function from the new main, otherwise the
// main is going to be empty
static bool createNewMain(llvm::Module *M, bool call_entry = false)
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

    if (call_entry) {
        assert(entry_func != "main" && "Calling main from main");
        llvm::Function *entry = M->getFunction(entry_func);
        assert(entry && "The entry function is not present in the module");

        // TODO: we should set the arguments to undef
        llvm::CallInst::Create(entry, "entry", blk);
    }

    llvm::Type *Ty = main_func->getReturnType();
    llvm::Value *retval = nullptr;
    if (Ty->isIntegerTy())
        retval = llvm::ConstantInt::get(Ty, 0);
    llvm::ReturnInst::Create(ctx, retval, blk);

    return true;
}

static std::vector<std::string> splitList(const std::string& opt, char sep = ',')
{
    std::vector<std::string> ret;
    if (opt.empty())
        return ret;

    size_t old_pos = 0;
    size_t pos = 0;
    while (true) {
        old_pos = pos;

        pos = opt.find(sep, pos);
        ret.push_back(opt.substr(old_pos, pos - old_pos));

        if (pos == std::string::npos)
            break;
        else
            ++pos;
    }

    return ret;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
splitStringVector(std::vector<std::string>& vec, std::function<bool(std::string&)> cmpFunc)
{
    std::vector<std::string> part1;
    std::vector<std::string> part2;

    for (auto& str : vec) {
        if (cmpFunc(str)) {
            part1.push_back(std::move(str));
        } else {
            part2.push_back(std::move(str));
        }
    }

    return {part1, part2};
}

static bool usesTheVariable(LLVMDependenceGraph& dg, const llvm::Value *v, const std::string& var)
{
    auto ptrNode = dg.getPTA()->getPointsTo(v);
    if (!ptrNode)
        return true; // it may be a definition of the variable, we do not know

    for (const auto& ptr : ptrNode->pointsTo) {
        if (ptr.isUnknown())
            return true; // it may be a definition of the variable, we do not know

        auto alloca = ptr.target->getUserData<llvm::Value>();
        if (!alloca)
            continue;

        if (const llvm::AllocaInst *AI = llvm::dyn_cast<llvm::AllocaInst>(alloca)) {
            auto name = valuesToVariables.find(AI);
            if (name != valuesToVariables.end()) {
                if (name->second == var)
                    return true;
            }
        }
    }

    return false;
}

template <typename InstT>
static bool useOfTheVar(LLVMDependenceGraph& dg, const llvm::Instruction& I, const std::string& var)
{
    // check that we store to that variable
    const InstT *tmp = llvm::dyn_cast<InstT>(&I);
    if (!tmp)
        return false;

    return usesTheVariable(dg, tmp->getPointerOperand(), var);
}

static bool isStoreToTheVar(LLVMDependenceGraph& dg, const llvm::Instruction& I, const std::string& var)
{
    return useOfTheVar<llvm::StoreInst>(dg, I, var);
}

static bool isLoadOfTheVar(LLVMDependenceGraph& dg, const llvm::Instruction& I, const std::string& var)
{
    return useOfTheVar<llvm::LoadInst>(dg, I, var);
}


static bool instMatchesCrit(LLVMDependenceGraph& dg,
                            const llvm::Instruction& I,
                            const std::vector<std::pair<int, std::string>>& parsedCrit)
{
    for (const auto& c : parsedCrit) {
        auto& Loc = I.getDebugLoc();
        if (!Loc)
            continue;
        if (static_cast<int>(Loc.getLine()) != c.first)
            continue;

        if (isStoreToTheVar(dg, I, c.second) ||
            isLoadOfTheVar(dg, I, c.second)) {
            llvm::errs() << "Matched line " << c.first << " with variable "
                         << c.second << " to:\n" << I << "\n";
            return true;
        }
    }

    return false;
}

static bool globalMatchesCrit(const llvm::GlobalVariable& G,
                              const std::vector<std::pair<int, std::string>>& parsedCrit)
{
    for (const auto& c : parsedCrit) {
        if (c.first != -1)
            continue;
        if (c.second == G.getName().str()) {
            llvm::errs() << "Matched global variable "
                         << c.second << " to:\n" << G << "\n";
            return true;
        }
    }

    return false;
}

static inline bool isNumber(const std::string& s) {
    assert(!s.empty());

    for (const auto c : s)
        if (!isdigit(c))
            return false;

    return true;
}

static void getLineCriteriaNodes(LLVMDependenceGraph& dg,
                                 std::vector<std::string>& criteria,
                                 std::set<LLVMNode *>& nodes)
{
    assert(!criteria.empty() && "No criteria given");

    std::vector<std::pair<int, std::string>> parsedCrit;
    for (auto& crit : criteria) {
        auto parts = splitList(crit, ':');
        assert(parts.size() == 2);

        // parse the line number
        if (parts[0].empty()) {
            // global variable
            parsedCrit.emplace_back(-1, parts[1]);
        } else if (isNumber(parts[0])) {
            int line = atoi(parts[0].c_str());
            if (line > 0)
                parsedCrit.emplace_back(line, parts[1]);
        } else {
            llvm::errs() << "Invalid line: '" << parts[0] << "'. "
                         << "Needs to be a number or empty for global variables.\n";
        }
    }

    assert(!parsedCrit.empty() && "Failed parsing criteria");

    // create the mapping from LLVM values to C variable names
    for (auto& it : getConstructedFunctions()) {
        for (auto& I : llvm::instructions(*llvm::cast<llvm::Function>(it.first))) {
            if (const llvm::DbgDeclareInst *DD = llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
                auto val = DD->getAddress();
                valuesToVariables[val] = DD->getVariable()->getName().str();
            } else if (const llvm::DbgValueInst *DV
                        = llvm::dyn_cast<llvm::DbgValueInst>(&I)) {
                auto val = DV->getValue();
                valuesToVariables[val] = DV->getVariable()->getName().str();
            }
        }
    }

    if (valuesToVariables.empty()) {
        llvm::errs() << "No debugging information found in program,\n"
                     << "slicing criteria with lines and variables will not work.\n"
                     << "You can still use the criteria based on call sites ;)\n";
        return;
    }

    // map line criteria to nodes
    for (auto& it : getConstructedFunctions()) {
        for (auto& I : llvm::instructions(*llvm::cast<llvm::Function>(it.first))) {
            if (instMatchesCrit(dg, I, parsedCrit)) {
                LLVMNode *nd = dg.getNode(&I);
                assert(nd);
                nodes.insert(nd);
            }
        }
    }

    for (auto& G : dg.getModule()->globals()) {
        if (globalMatchesCrit(G, parsedCrit)) {
            LLVMNode *nd = dg.getGlobalNode(&G);
            assert(nd);
            nodes.insert(nd);
        }
    }
}

static std::set<LLVMNode *> getSlicingCriteriaNodes(LLVMDependenceGraph& dg)
{
    std::set<LLVMNode *> nodes;
    std::vector<std::string> criteria = splitList(slicing_criteria);
    assert(!criteria.empty() && "Did not get slicing criteria");

    std::vector<std::string> line_criteria;
    std::vector<std::string> node_criteria;
    std::tie(line_criteria, node_criteria)
        = splitStringVector(criteria, [](std::string& s) -> bool
            { return s.find(':') != std::string::npos; }
          );

    // if user wants to slice with respect to the return of main,
    // insert the ret instructions to the nodes.
    for (const auto& c : node_criteria) {
        if (c == "ret") {
            LLVMNode *exit = dg.getExit();
            // We could insert just the exit node, but this way we will
            // get annotations to the functions.
            for (auto it = exit->rev_control_begin(), et = exit->rev_control_end();
                 it != et; ++it) {
                nodes.insert(*it);
            }
        }
    }

    // map the criteria to nodes
    if (!node_criteria.empty())
        dg.getCallSites(node_criteria, &nodes);
    if (!line_criteria.empty())
        getLineCriteriaNodes(dg, line_criteria, nodes);

    return nodes;
}

struct SlicerOptions {
    llvmdg::LLVMDependenceGraphOptions dgOptions{};

    std::vector<std::string> untouchedFunctions;
    // FIXME: get rid of this once we got the secondary SC
    std::vector<std::string> additionalSlicingCriteria;
};


/// --------------------------------------------------------------------
//   - Slicer class -
//
//  The main class that represents slicer and covers the elementary
//  functionality
/// --------------------------------------------------------------------
class Slicer {
    llvm::Module *M{};
    const SlicerOptions& _options;
    AnnotationOptsT _annotationOptions{};

    LLVMDependenceGraphBuilder _builder;
    std::unique_ptr<LLVMDependenceGraph> _dg{};

    LLVMSlicer slicer;

    uint32_t slice_id = 0;
    bool got_slicing_criteria = true;

public:
    Slicer(llvm::Module *mod,
           const SlicerOptions& opts,
           AnnotationOptsT ao)
    : M(mod), _options(opts),
      _annotationOptions(ao),
      _builder(mod, _options.dgOptions) { assert(mod && "Need module"); }

    const LLVMDependenceGraph& getDG() const { return *_dg.get(); }
    LLVMDependenceGraph& getDG() { return *_dg.get(); }

    // shared by old and new analyses
    bool mark()
    {
        assert(_dg && "mark() called without the dependence graph built");

        debug::TimeMeasure tm;

        // check for slicing criterion here, because
        // we might have built new subgraphs that contain
        // it during points-to analysis
        std::set<LLVMNode *> criteria_nodes = getSlicingCriteriaNodes(getDG());
        got_slicing_criteria = true;
        if (criteria_nodes.empty()) {
            errs() << "Did not find slicing criterion: "
                   << slicing_criteria << "\n";
            got_slicing_criteria = false;
        }

        // if we found slicing criterion, compute the rest
        // of the graph. Otherwise just slice away the whole graph
        // Also compute the edges when the user wants to annotate
        // the file - due to debugging.
        if (got_slicing_criteria || (_annotationOptions != 0))
            _dg = _builder.computeDependencies(std::move(_dg));

        // don't go through the graph when we know the result:
        // only empty main will stay there. Just delete the body
        // of main and keep the return value
        if (!got_slicing_criteria)
            return createNewMain(M, entry_func != "main");

        // unmark this set of nodes after marking the relevant ones.
        // Used to mimic the Weissers algorithm
        std::set<LLVMNode *> unmark;

        if (remove_slicing_criteria)
            unmark = criteria_nodes;

        _dg->getCallSites(_options.additionalSlicingCriteria, &criteria_nodes);

        // do not slice __VERIFIER_assume at all
        // FIXME: do this optional
        for (auto& funcName : _options.untouchedFunctions)
            slicer.keepFunctionUntouched(funcName.c_str());

        slice_id = 0xdead;

        tm.start();
        for (LLVMNode *start : criteria_nodes)
            slice_id = slicer.mark(start, slice_id, forward_slice);

        // if we have some nodes in the unmark set, unmark them
        for (LLVMNode *nd : unmark)
            nd->setSlice(0);

        tm.stop();
        tm.report("INFO: Finding dependent nodes took");

        // print debugging llvm IR if user asked for it
        if (_annotationOptions != 0)
            annotate(M, _annotationOptions,
                     _builder.getPTA(),
                     _builder.getRDA(),
                     &criteria_nodes);

        return true;
    }

    bool slice()
    {
        // we created an empty main in this case
        if (!got_slicing_criteria)
            return true;

        if (slice_id == 0) {
            if (!mark())
                return false;
        }

        debug::TimeMeasure tm;

        tm.start();
        slicer.slice(_dg.get(), nullptr, slice_id);

        tm.stop();
        tm.report("INFO: Slicing dependence graph took");

        analysis::SlicerStatistics& st = slicer.getStatistics();
        errs() << "INFO: Sliced away " << st.nodesRemoved
               << " from " << st.nodesTotal << " nodes in DG\n";

        return true;
    }

    bool buildDG() {
        _dg = std::move(_builder.constructCFGOnly());

        if (!_dg) {
            llvm::errs() << "Building the dependence graph failed!\n";
            return false;
        }

        return true;
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
    const char *keep[] = {entry_func.c_str(), "klee_assume", NULL};

    // when erasing while iterating the slicer crashes
    // so set the to be erased values into container
    // and then erase them
    std::set<Function *> funs;
    std::set<GlobalVariable *> globals;
    std::set<GlobalAlias *> aliases;

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

    for (GlobalAlias& ga : M->getAliasList()) {
        if (ga.hasNUses(0))
            aliases.insert(&ga);
    }

    for (Function *f : funs)
        f->eraseFromParent();
    for (GlobalVariable *gv : globals)
        gv->eraseFromParent();
    for (GlobalAlias *ga : aliases)
        ga->eraseFromParent();

    return (!funs.empty() || !globals.empty() || !aliases.empty());
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

    // iterate over all functions in module
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

#if ((LLVM_VERSION_MAJOR >= 4) || (LLVM_VERSION_MINOR >= 5))
    return !llvm::verifyModule(*M, &llvm::errs());
#else
    return !llvm::verifyModule(*M, llvm::PrintMessageAction);
#endif
}

static void replace_suffix(std::string& fl, const std::string& with)
{
    if (fl.size() > 2) {
        if (fl.compare(fl.size() - 2, 2, ".o") == 0)
            fl.replace(fl.end() - 2, fl.end(), with);
        else if (fl.compare(fl.size() - 3, 3, ".bc") == 0)
            fl.replace(fl.end() - 3, fl.end(), with);
        else
            fl += with;
    } else {
        fl += with;
    }
}
static bool write_module(llvm::Module *M)
{
    // compose name if not given
    std::string fl;
    if (!output.empty()) {
        fl = output;
    } else {
        fl = llvmfile;
        replace_suffix(fl, ".sliced");
    }

    // open stream to write to
    std::ofstream ofs(fl);
    llvm::raw_os_ostream ostream(ofs);

    // write the module
    errs() << "INFO: saving sliced module to: " << fl.c_str() << "\n";

#if (LLVM_VERSION_MAJOR > 6)
    llvm::WriteBitcodeToFile(*M, ostream);
#else
    llvm::WriteBitcodeToFile(M, ostream);
#endif

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

static void dump_dg_to_dot(LLVMDependenceGraph& dg, bool bb_only = false,
                           uint32_t dump_opts = debug::PRINT_DD | debug::PRINT_CD | debug::PRINT_USE,
                           const char *suffix = nullptr)
{
    // compose new name
    std::string fl(llvmfile);
    if (suffix)
        replace_suffix(fl, suffix);
    else
        replace_suffix(fl, ".dot");

    errs() << "INFO: Dumping DG to to " << fl << "\n";

    if (bb_only) {
        debug::LLVMDGDumpBlocks dumper(&dg, dump_opts, fl.c_str());
        dumper.dump();
    } else {
        debug::LLVMDG2Dot dumper(&dg, dump_opts, fl.c_str());
        dumper.dump();
    }
}

static AnnotationOptsT parseAnnotationOpt(const std::string& annot)
{
    if (annot.empty())
        return {};

    AnnotationOptsT opts{};
    std::vector<std::string> lst = splitList(annot);
    for (const std::string& opt : lst) {
        if (opt == "dd")
            opts |= AnnotationOptsT::ANNOTATE_DD;
        else if (opt == "cd")
            opts |= AnnotationOptsT::ANNOTATE_CD;
        else if (opt == "rd")
            opts |= AnnotationOptsT::ANNOTATE_RD;
        else if (opt == "pta")
            opts |= AnnotationOptsT::ANNOTATE_PTR;
        else if (opt == "slice" || opt == "sl" || opt == "slicer")
            opts |= AnnotationOptsT::ANNOTATE_SLICE;
    }

    return opts;
}

int main(int argc, char *argv[])
{
#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 9
    llvm::sys::PrintStackTraceOnErrorSignal();
#else
    llvm::sys::PrintStackTraceOnErrorSignal(llvm::StringRef());
#endif
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

    llvm::cl::opt<bool> dump_dg("dump-dg",
        llvm::cl::desc("Dump dependence graph to dot (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> dump_dg_only("dump-dg-only",
        llvm::cl::desc("Only dump dependence graph to dot,"
                       " do not slice the module (default=false)."),
        llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

    llvm::cl::opt<bool> bb_only("dump-bb-only",
        llvm::cl::desc("Only dump basic blocks of dependence graph to dot"
                       " (default=false)."),
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
    // this method is available since LLVM 3.7
#if ((LLVM_VERSION_MAJOR > 3)\
      || ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR >= 7)))
    llvm::cl::HideUnrelatedOptions(SlicingOpts);
#endif
# if ((LLVM_VERSION_MAJOR >= 6))
    llvm::cl::SetVersionPrinter([](llvm::raw_ostream&){ printf("%s\n", GIT_VERSION); });
#else
    llvm::cl::SetVersionPrinter([](){ printf("%s\n", GIT_VERSION); });
#endif
    llvm::cl::ParseCommandLineOptions(argc, argv);

    AnnotationOptsT annotationOpts = parseAnnotationOpt(annot);
    uint32_t dump_opts = debug::PRINT_CFG | debug::PRINT_DD | debug::PRINT_CD;
    // dump_dg_only implies dumg_dg
    if (dump_dg_only)
        dump_dg = true;

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 5))
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
    SlicerOptions options;
    // we do not want to slice away any assumptions
    // about the code
    // FIXME: do this optional only for SV-COMP
    options.additionalSlicingCriteria = {
        "__VERIFIER_assume",
        "__VERIFIER_exit",
        "klee_assume",
    };

    options.untouchedFunctions = {
        "__VERIFIER_assume",
        "__VERIFIER_exit"
    };

    options.dgOptions.PTAOptions.entryFunction = entry_func;
    options.dgOptions.PTAOptions.fieldSensitivity
                                    = analysis::Offset(pta_field_sensitivie);
    options.dgOptions.PTAOptions.analysisType = ptaType;

    options.dgOptions.RDAOptions.entryFunction = entry_func;
    options.dgOptions.RDAOptions.strongUpdateUnknown = rd_strong_update_unknown;
    options.dgOptions.RDAOptions.undefinedArePure = undefined_are_pure;
    options.dgOptions.RDAOptions.analysisType = rdaType;

    // FIXME: add classes for CD and DEF-USE settings
    options.dgOptions.cdAlgorithm = cdAlgorithm;
    options.dgOptions.DUUndefinedArePure = undefined_are_pure;

    Slicer slicer(M, options, annotationOpts);

    // build the dependence graph, so that we can dump it if desired
    if (!slicer.buildDG()) {
        errs() << "ERROR: Failed building DG\n";
        return 1;
    }

    // mark nodes that are going to be in the slice
    slicer.mark();

    if (dump_dg) {
        dump_dg_to_dot(slicer.getDG(), bb_only, dump_opts);

        if (dump_dg_only)
            return 0;
    }

    // slice the graph
    if (!slicer.slice()) {
        errs() << "ERROR: Slicing failed\n";
        return 1;
    }

    if (dump_dg) {
        dump_dg_to_dot(slicer.getDG(), bb_only,
                       dump_opts, ".sliced.dot");
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
