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

#include "llvm-slicer.h"
#include "llvm-slicer-opts.h"

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

#include "dg/llvm/LLVMDG2Dot.h"
#include "llvm/LLVMDGAssemblyAnnotationWriter.h"

using namespace dg;

using llvm::errs;
using dg::analysis::LLVMPointerAnalysisOptions;
using dg::analysis::LLVMReachingDefinitionsAnalysisOptions;

using AnnotationOptsT
    = dg::debug::LLVMDGAssemblyAnnotationWriter::AnnotationOptsT;


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

llvm::cl::opt<bool> dump_bb_only("dump-bb-only",
    llvm::cl::desc("Only dump basic blocks of dependence graph to dot"
                   " (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<std::string> annotationOpts("annotate",
    llvm::cl::desc("Save annotated version of module as a text (.ll).\n"
                   "(dd: data dependencies, cd:control dependencies,\n"
                   "rd: reaching definitions, pta: points-to information,\n"
                   "slice: comment out what is going to be sliced away, etc.)\n"
                   "for more options, use comma separated list"),
    llvm::cl::value_desc("val1,val2,..."), llvm::cl::init(""),
    llvm::cl::cat(SlicingOpts));


// mapping of AllocaInst to the names of C variables
std::map<const llvm::Value *, std::string> valuesToVariables;

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


class ModuleWriter {
    const SlicerOptions& options;
    llvm::Module *M;

public:
    ModuleWriter(const SlicerOptions& o,
                 llvm::Module *m)
    : options(o), M(m) {}

    int cleanAndSaveModule(bool should_verify_module = true) {
        // remove unneeded parts of the module
        removeUnusedFromModule();

        // fix linkage of declared functions (if needs to be fixed)
        makeDeclarationsExternal();

        return saveModule(should_verify_module);
    }

    int saveModule(bool should_verify_module = true)
    {
        if (should_verify_module)
            return verifyAndWriteModule();
        else
            return writeModule();
    }

    void removeUnusedFromModule()
    {
        bool fixpoint;

        do {
            fixpoint = _removeUnusedFromModule();
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
    void makeDeclarationsExternal()
    {
        using namespace llvm;

        // iterate over all functions in module
        for (auto& F : *M) {
            if (F.size() == 0) {
                // this will make sure that the linkage has right type
                F.deleteBody();
            }
        }
    }

private:
    bool writeModule() {
        // compose name if not given
        std::string fl;
        if (!options.outputFile.empty()) {
            fl = options.outputFile;
        } else {
            fl = options.inputFile;
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

    bool verifyModule()
    {
        // the verifyModule function returns false if there
        // are no errors

#if ((LLVM_VERSION_MAJOR >= 4) || (LLVM_VERSION_MINOR >= 5))
        return !llvm::verifyModule(*M, &llvm::errs());
#else
       return !llvm::verifyModule(*M, llvm::PrintMessageAction);
#endif
    }


    int verifyAndWriteModule()
    {
        if (!verifyModule()) {
            errs() << "ERR: Verifying module failed, the IR is not valid\n";
            errs() << "INFO: Saving anyway so that you can check it\n";
            return 1;
        }

        if (!writeModule()) {
            errs() << "Saving sliced module failed\n";
            return 1;
        }

        // exit code
        return 0;
    }

    bool _removeUnusedFromModule()
    {
        using namespace llvm;
        // do not slice away these functions no matter what
        // FIXME do it a vector and fill it dynamically according
        // to what is the setup (like for sv-comp or general..)
        const char *keep[] = {options.dgOptions.entryFunction.c_str(),
                              "klee_assume", nullptr};

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

};

static void maybe_print_statistics(llvm::Module *M, const char *prefix = nullptr)
{
    if (!statistics)
        return;

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

class DGDumper {
    const SlicerOptions& options;
    LLVMDependenceGraph *dg;
    bool bb_only{false};
    uint32_t dump_opts{debug::PRINT_DD | debug::PRINT_CD | debug::PRINT_USE};

public:
    DGDumper(const SlicerOptions& opts,
             LLVMDependenceGraph *dg,
             bool bb_only = false,
             uint32_t dump_opts = debug::PRINT_DD | debug::PRINT_CD | debug::PRINT_USE)
    : options(opts), dg(dg), bb_only(bb_only), dump_opts(dump_opts) {}

    void dumpToDot(const char *suffix = nullptr) {
        // compose new name
        std::string fl(options.inputFile);
        if (suffix)
            replace_suffix(fl, suffix);
        else
            replace_suffix(fl, ".dot");

        errs() << "INFO: Dumping DG to to " << fl << "\n";

        if (bb_only) {
            debug::LLVMDGDumpBlocks dumper(dg, dump_opts, fl.c_str());
            dumper.dump();
        } else {
            debug::LLVMDG2Dot dumper(dg, dump_opts, fl.c_str());
            dumper.dump();
        }
    }
};

class ModuleAnnotator {
    const SlicerOptions& options;
    LLVMDependenceGraph *dg;
    AnnotationOptsT annotationOptions;

public:
    ModuleAnnotator(const SlicerOptions& o,
                    LLVMDependenceGraph *dg,
                    AnnotationOptsT annotO)
    : options(o), dg(dg), annotationOptions(annotO) {}

    bool shouldAnnotate() const { return annotationOptions != 0; }

    void annotate(const std::set<LLVMNode *> *criteria = nullptr)
    {
        // compose name
        std::string fl(options.inputFile);
        fl.replace(fl.end() - 3, fl.end(), "-debug.ll");

        // open stream to write to
        std::ofstream ofs(fl);
        llvm::raw_os_ostream outputstream(ofs);

        std::string module_comment =
        "; -- Generated by llvm-slicer --\n"
        ";   * slicing criteria: '" + options.slicingCriteria + "'\n" +
        ";   * forward slice: '" + std::to_string(options.forwardSlicing) + "'\n" +
        ";   * remove slicing criteria: '"
             + std::to_string(options.removeSlicingCriteria) + "'\n" +
        ";   * undefined are pure: '"
             + std::to_string(options.dgOptions.RDAOptions.undefinedArePure) + "'\n" +
        ";   * pointer analysis: ";
        if (options.dgOptions.PTAOptions.analysisType
                == LLVMPointerAnalysisOptions::AnalysisType::fi)
            module_comment += "flow-insensitive\n";
        else if (options.dgOptions.PTAOptions.analysisType
                    == LLVMPointerAnalysisOptions::AnalysisType::fs)
            module_comment += "flow-sensitive\n";
        else if (options.dgOptions.PTAOptions.analysisType
                    == LLVMPointerAnalysisOptions::AnalysisType::inv)
            module_comment += "flow-sensitive with invalidate\n";

        module_comment+= ";   * PTA field sensitivity: ";
        if (options.dgOptions.PTAOptions.fieldSensitivity == Offset::UNKNOWN)
            module_comment += "full\n\n";
        else
            module_comment
                += std::to_string(*options.dgOptions.PTAOptions.fieldSensitivity)
                   + "\n\n";

        errs() << "INFO: Saving IR with annotations to " << fl << "\n";
        auto annot
            = new dg::debug::LLVMDGAssemblyAnnotationWriter(annotationOptions,
                                                            dg->getPTA(),
                                                            dg->getRDA(),
                                                            criteria);
        annot->emitModuleComment(std::move(module_comment));
        llvm::Module *M = dg->getModule();
        M->print(outputstream, annot);

        delete annot;
    }
};


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
splitStringVector(std::vector<std::string>& vec,
                  std::function<bool(std::string&)> cmpFunc)
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

static bool usesTheVariable(LLVMDependenceGraph& dg,
                            const llvm::Value *v,
                            const std::string& var)
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
static bool useOfTheVar(LLVMDependenceGraph& dg,
                        const llvm::Instruction& I,
                        const std::string& var)
{
    // check that we store to that variable
    const InstT *tmp = llvm::dyn_cast<InstT>(&I);
    if (!tmp)
        return false;

    return usesTheVariable(dg, tmp->getPointerOperand(), var);
}

static bool isStoreToTheVar(LLVMDependenceGraph& dg,
                            const llvm::Instruction& I,
                            const std::string& var)
{
    return useOfTheVar<llvm::StoreInst>(dg, I, var);
}

static bool isLoadOfTheVar(LLVMDependenceGraph& dg,
                           const llvm::Instruction& I,
                           const std::string& var)
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

static std::set<LLVMNode *> getSlicingCriteriaNodes(LLVMDependenceGraph& dg,
                                                    const std::string& slicingCriteria)
{
    std::set<LLVMNode *> nodes;
    std::vector<std::string> criteria = splitList(slicingCriteria);
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

static AnnotationOptsT parseAnnotationOptions(const std::string& annot)
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

std::unique_ptr<llvm::Module> parseModule(llvm::LLVMContext& context,
                                          const SlicerOptions& options)
{
    llvm::SMDiagnostic SMD;

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 5))
    auto _M = llvm::ParseIRFile(options.inputFile, SMD, context);
    auto M = std::unique_ptr<llvm::Module>(_M);
#else
    auto M = llvm::parseIRFile(options.inputFile, SMD, context);
    // _M is unique pointer, we need to get Module *
#endif

    if (!M) {
        SMD.print("llvm-slicer", llvm::errs());
    }

    return M;
}

#ifndef USING_SANITIZERS
void setupStackTraceOnError(int argc, char *argv[])
{

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 9
    llvm::sys::PrintStackTraceOnErrorSignal();
#else
    llvm::sys::PrintStackTraceOnErrorSignal(llvm::StringRef());
#endif
    llvm::PrettyStackTraceProgram X(argc, argv);

}
#else
void setupStackTraceOnError(int, char **) {}
#endif // not USING_SANITIZERS


int main(int argc, char *argv[])
{
    setupStackTraceOnError(argc, argv);

    SlicerOptions options = parseSlicerOptions(argc, argv);

    // dump_dg_only implies dumg_dg
    if (dump_dg_only)
        dump_dg = true;

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> M = parseModule(context, options);
    if (!M) {
        llvm::errs() << "Failed parsing '" << options.inputFile << "' file:\n";
        return 1;
    }

    if (!M->getFunction(options.dgOptions.entryFunction)) {
        llvm::errs() << "The entry function not found: "
                     << options.dgOptions.entryFunction << "\n";
        return 1;
    }

    maybe_print_statistics(M.get(), "Statistics before ");

    // remove unused from module, we don't need that
    ModuleWriter writer(options, M.get());
    writer.removeUnusedFromModule();

    if (remove_unused_only) {
        errs() << "INFO: removed unused parts of module, exiting...\n";
        maybe_print_statistics(M.get(), "Statistics after ");
        return writer.saveModule(should_verify_module);
    }

    /// ---------------
    // slice the code
    /// ---------------

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

    Slicer slicer(M.get(), options);
    if (!slicer.buildDG()) {
        errs() << "ERROR: Failed building DG\n";
        return 1;
    }

    ModuleAnnotator annotator(options, &slicer.getDG(),
                              parseAnnotationOptions(annotationOpts));

    auto criteria_nodes = getSlicingCriteriaNodes(slicer.getDG(),
                                                  options.slicingCriteria);
    if (criteria_nodes.empty()) {
        llvm::errs() << "Did not find slicing criteria: '"
                     << options.slicingCriteria << "'\n";
        if (annotator.shouldAnnotate()) {
            slicer.computeDependencies();
            annotator.annotate();
        }

        if (!slicer.createEmptyMain())
            return 1;

        maybe_print_statistics(M.get(), "Statistics after ");
        return writer.cleanAndSaveModule(should_verify_module);
    }

    // mark nodes that are going to be in the slice
    if (!slicer.mark(criteria_nodes)) {
        llvm::errs() << "Finding dependent nodes failed\n";
        return 1;
    }

    // print debugging llvm IR if user asked for it
    if (annotator.shouldAnnotate())
        annotator.annotate(&criteria_nodes);

    DGDumper dumper(options, &slicer.getDG(), dump_bb_only);
    if (dump_dg) {
        dumper.dumpToDot();

        if (dump_dg_only)
            return 0;
    }

    // slice the graph
    if (!slicer.slice()) {
        errs() << "ERROR: Slicing failed\n";
        return 1;
    }

    if (dump_dg) {
        dumper.dumpToDot(".sliced.dot");
    }

    // remove unused from module again, since slicing
    // could and probably did make some other parts unused
    maybe_print_statistics(M.get(), "Statistics after ");
    return writer.cleanAndSaveModule(should_verify_module);
}
