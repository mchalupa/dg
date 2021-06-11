#include <cassert>
#include <set>
#include <string>
#include <vector>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/Config/llvm-config.h>

#if (LLVM_VERSION_MAJOR < 3)
#error "Unsupported version of LLVM"
#endif

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/IR/CFG.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 7
#include <llvm/IR/LLVMContext.h>
#endif
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_os_ostream.h>
SILENCE_LLVM_WARNINGS_POP

#include "dg/ADT/Queue.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMNode.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/tools/llvm-slicer-utils.h"

using namespace dg;

using llvm::errs;

// mapping of AllocaInst to the names of C variables
static std::map<const llvm::Value *, std::string> valuesToVariables;

static inline bool isNumber(const std::string &s) {
    assert(!s.empty());

    for (const auto c : s)
        if (!isdigit(c))
            return false;

    return true;
}

static inline bool isTheVar(const llvm::Value *val, const std::string &var) {
    auto name = valuesToVariables.find(val);
    if (name != valuesToVariables.end()) {
        if (name->second == var) {
            return true;
        }
    }
    return false;
}

static bool usesTheVariable(const llvm::Instruction &I, const std::string &var,
                            bool isglobal = false,
                            LLVMPointerAnalysis *pta = nullptr) {
    if (!I.mayReadOrWriteMemory())
        return false;

    if (!pta) {
        // try basic cases that we can decide without PTA
        using namespace llvm;
        if (auto *S = dyn_cast<StoreInst>(&I)) {
            auto *A = S->getPointerOperand()->stripPointerCasts();
            if (isa<AllocaInst>(A) && !isTheVar(A, var)) {
                return false;
            }
        } else if (auto *L = dyn_cast<LoadInst>(&I)) {
            auto *A = L->getPointerOperand()->stripPointerCasts();
            if (isa<AllocaInst>(A) && !isTheVar(A, var)) {
                return false;
            }
        }
        return true;
    }

    auto memacc = pta->getAccessedMemory(&I);
    if (memacc.first) {
        // PTA has no information, it may be a definition of the variable,
        // we do not know
        llvm::errs() << "WARNING: matched due to a lack of information: " << I
                     << "\n";
        return true;
    }

    for (const auto &region : memacc.second) {
        if (isglobal &&
            !llvm::isa<llvm::GlobalVariable>(region.pointer.value)) {
            continue;
        }
        if (isTheVar(region.pointer.value, var)) {
            return true;
        }
    }

    return false;
}

static bool funHasAddrTaken(const llvm::Function *fun) {
    using namespace llvm;

    for (auto use_it = fun->use_begin(), use_end = fun->use_end();
         use_it != use_end; ++use_it) {
#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 5))
        Value *user = *use_it;
#else
        Value *user = use_it->getUser();
#endif
        if (auto *C = dyn_cast<CallInst>(user)) {
            // FIXME: use getCalledOperand() and strip the casts
            if (fun != C->getCalledFunction()) {
                return true;
            }
        } else if (auto *S = dyn_cast<StoreInst>(user)) {
            if (S->getValueOperand()->stripPointerCasts() == fun) {
                return true;
            } else {
                llvm::errs() << "Unhandled function use: " << *user << "\n";
                return true;
            }
            // FIXME: the function can be in a cast instruction that is just
            // used in call,
            //   we can detect that
        } else {
            llvm::errs() << "Unhandled function use: " << *user << "\n";
            return true;
        }
    }
    return false;
}

static bool funHasAddrTaken(const llvm::Module *M, const std::string &name) {
    const auto *fun = M->getFunction(name);
    if (!fun) {
        // the module does not have this function
        return false;
    }
    return funHasAddrTaken(fun);
}

static bool instIsCallOf(const llvm::Instruction &I, const std::string &name,
                         LLVMPointerAnalysis *pta = nullptr) {
    const auto *C = llvm::dyn_cast<llvm::CallInst>(&I);
    if (!C)
        return false;

    auto *fun = C->getCalledFunction();
    if (fun) {
        return name == fun->getName().str();
    }

#if LLVM_VERSION_MAJOR >= 8
    auto *V = C->getCalledOperand()->stripPointerCasts();
#else
    auto *V = C->getCalledValue()->stripPointerCasts();
#endif

    if (!pta) {
        auto *M = I.getParent()->getParent()->getParent();
        return funHasAddrTaken(M, name);
    }

    auto pts = pta->getLLVMPointsTo(V);
    if (pts.empty()) {
        auto *M = I.getParent()->getParent()->getParent();
        return funHasAddrTaken(M, name);
    }

    for (const auto &ptr : pts) {
        fun = llvm::dyn_cast<llvm::Function>(ptr.value);
        if (!fun)
            continue;
        if (name == fun->getName().str())
            return true;
    }

    return false;
}

static bool fileMatch(const std::string &file, const llvm::Instruction &I) {
#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 7
    const auto *F = I.getParent()->getParent();
    const auto *subprog = llvm::cast<llvm::DISubprogram>(
            F->getMetadata(llvm::LLVMContext::MD_dbg));
#else
    const auto *subprog = I.getFunction()->getSubprogram();
#endif
    return subprog->getFile()->getFilename() == file;
}

static bool fileMatch(const std::string &file, const llvm::GlobalVariable &G) {
#if LLVM_VERSION_MAJOR < 4
    return true;
#else
    llvm::SmallVector<llvm::DIGlobalVariableExpression *, 2> GVs;
    G.getDebugInfo(GVs);
    bool has_match = false;
    for (auto *GV : GVs) {
        auto *var = GV->getVariable();
        if (var->getFile()->getFilename() == file) {
            has_match = true;
            break;
        }
    }
    return has_match;
#endif
}

static bool instMatchesCrit(const llvm::Instruction &I, const std::string &fun,
                            unsigned line, const std::string &obj,
                            LLVMPointerAnalysis *pta = nullptr) {
    // function match?
    if (!fun.empty() && I.getParent()->getParent()->getName().str() != fun)
        return false;

    // line match?
    if (line > 0) {
        const auto &Loc = I.getDebugLoc();
#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
        if (Loc.getLine() != line)
#else
        if (!Loc || line != Loc.getLine())
#endif
        {
            return false;
        }
    }

    if (obj.empty()) {
        // we passed the line check and we have no obj to check
        return true;
    }

    // TODO: allow speficy namespaces, not only global/non-global
    bool isvar = obj[0] == '&';
    std::string objname;
    if (isvar) {
        objname = obj.substr(1);
    } else {
        objname = obj;
    }

    bool isglobal = objname[0] == '@';
    if (isglobal) {
        objname = obj.substr(1);
    }

    auto len = objname.length();
    bool isfunc = len > 2 && objname.compare(len - 2, 2, "()") == 0;
    if (isfunc) {
        objname = objname.substr(0, len - 2);
    }

    if (isvar && isfunc) {
        static std::set<std::string> reported;
        if (reported.insert(obj).second) {
            llvm::errs() << "ERROR: ignoring invalid criterion (var and func "
                            "at the same time: "
                         << obj << "\n";
        }
        return false;
    }

    // obj match?
    if (!isvar && instIsCallOf(I, objname, pta)) {
        return true;
    } // else fall through to check the vars

    if (!isfunc && usesTheVariable(I, objname, isglobal, pta)) {
        return true;
    }

    return false;
}

static bool globalMatchesCrit(const llvm::GlobalVariable &G, unsigned line,
                              const std::string &obj) {
    if (obj != G.getName().str()) {
        return false;
    }

#if LLVM_VERSION_MAJOR < 4
    return true;
#else
    if (line > 0) {
        llvm::SmallVector<llvm::DIGlobalVariableExpression *, 2> GVs;
        G.getDebugInfo(GVs);
        bool has_match = false;
        for (auto *GV : GVs) {
            auto *var = GV->getVariable();
            if (var->getLine() == line) {
                has_match = true;
                break;
            }
        }
        if (!has_match)
            return false;
    }
#endif // LLVM >= 4

    return true;
}

static unsigned parseLine(const std::vector<std::string> &parts) {
    unsigned idx = -1;
    switch (parts.size()) {
    case 2:
        idx = 0;
        break;
    case 3:
        idx = 1;
        break;
    case 4:
        idx = 2;
        break;
    default:
        return 0;
    }

    assert(idx == 0 || idx <= 2);
    assert(idx < parts.size());

    if (parts[idx].empty() || parts[idx] == "*")
        return 0; // any line

    // will we support multiple lines separated by comma?
    if (!isNumber(parts[idx])) {
        llvm::errs() << "ERROR: invalid line number: " << parts[idx] << "\n";
        return 0;
    }

    return atoi(parts[idx].c_str());
}

static std::string parseFile(const std::vector<std::string> &parts) {
    if (parts.size() == 4)
        return parts[0];
    return "";
}

static std::string parseFun(const std::vector<std::string> &parts) {
    switch (parts.size()) {
    case 4:
        return parts[1];
    case 3:
        return parts[0];
    default:
        return "";
    }
}

static std::string parseObj(const std::vector<std::string> &parts) {
    assert(parts.size() > 0);
    return parts[parts.size() - 1];
}

static void getCriteriaInstructions(llvm::Module &M, LLVMPointerAnalysis *pta,
                                    const std::string &criterion,
                                    std::set<const llvm::Value *> &result,
                                    bool constructed_only = false) {
    assert(!criterion.empty() && "No criteria given");

    auto parts = splitList(criterion, '#');
    if (parts.size() > 4 || parts.empty()) {
        llvm::errs() << "WARNING: ignoring invalid slicing criterion: "
                     << criterion << "\n";
        return;
    }

    unsigned line = parseLine(parts);
    auto fun = parseFun(parts);
    auto obj = parseObj(parts);
    auto file = parseFile(parts);

    DBG(llvm - slicer, "Criterion file # fun # line # obj ==> "
                               << file << " # " << fun << " # " << line << " # "
                               << obj);

    if (!fun.empty() && obj.empty() && line == 0) {
        llvm::errs() << "WARNING: ignoring invalid slicing criterion: "
                     << criterion << "\n";
        return;
    }

    // try match globals
    DBG(llvm - slicer, "Checking global variables for slicing criteria");
    if (fun.empty()) {
        for (auto &G : M.globals()) {
            if (!file.empty() && !fileMatch(file, G))
                continue;
            if (globalMatchesCrit(G, line, obj)) {
                result.insert(&G);
            }
        }
    }

    if (constructed_only) {
        DBG(llvm - slicer,
            "Checking constructed functions for slicing criteria");

        for (auto &it : getConstructedFunctions()) {
            for (auto &I :
                 llvm::instructions(*llvm::cast<llvm::Function>(it.first))) {
                if (!file.empty() && !fileMatch(file, I))
                    continue;

                if (instMatchesCrit(I, fun, line, obj, pta)) {
                    result.insert(&I);
                }
            }
        }
    } else {
        DBG(llvm - slicer, "Checking all instructions for slicing criteria");

        for (auto &F : M) {
            for (auto &I : llvm::instructions(F)) {
                if (file != "" && !fileMatch(file, I))
                    continue;

                if (instMatchesCrit(I, fun, line, obj, pta)) {
                    result.insert(&I);
                }
            }
        }
    }
}

struct SlicingCriteriaSet {
    std::set<const llvm::Value *> primary;
    std::set<const llvm::Value *> secondary;

    SlicingCriteriaSet() = default;
    SlicingCriteriaSet(SlicingCriteriaSet &&) = default;
};

static std::set<const llvm::Value *>
mapToNextInstr(const std::set<const ::llvm::Value *> &vals) {
    std::set<const llvm::Value *> newset;
    for (const auto *val : vals) {
        const auto *I = llvm::dyn_cast<llvm::Instruction>(val);
        I = I ? I->getNextNode() : nullptr;
        if (!I) {
            llvm::errs() << "WARNING: unable to get next instr for " << *val
                         << "\n";
            continue;
        }
        newset.insert(I);
    }
    return newset;
}

static void initDebugInfo(LLVMDependenceGraph &dg) {
    if (!valuesToVariables.empty())
        return;

#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
    llvm::errs() << "WARNING: Variables names matching is not supported for "
                    "LLVM older than 3.7\n";
    llvm::errs() << "WARNING: The slicing criteria with variables names will "
                    "not work\n";
#else // LLVM >= 3.8
#if (LLVM_VERSION_MAJOR < 4)
    llvm::errs() << "WARNING: Function/global names matching is not supported "
                    "for LLVM older than 4\n";
    llvm::errs() << "WARNING: The slicing criteria with variables names will "
                    "not work well\n";
#endif
    // create the mapping from LLVM values to C variable names
    for (const auto &it : getConstructedFunctions()) {
        for (auto &I :
             llvm::instructions(*llvm::cast<llvm::Function>(it.first))) {
            if (const llvm::DbgDeclareInst *DD =
                        llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
                auto *val = DD->getAddress();
                valuesToVariables[val] = DD->getVariable()->getName().str();
            } else if (const llvm::DbgValueInst *DV =
                               llvm::dyn_cast<llvm::DbgValueInst>(&I)) {
                auto *val = DV->getValue();
                valuesToVariables[val] = DV->getVariable()->getName().str();
            }
        }
    }

    bool no_dbg = valuesToVariables.empty();
    if (no_dbg) {
        llvm::errs()
                << "No debugging information found in program, "
                << "slicing criteria with lines and variables will work\n"
                << "only for global variables. "
                << "You can still use the criteria based on call sites ;)\n";
    }

    for (const auto &GV : dg.getModule()->globals()) {
        valuesToVariables[&GV] = GV.getName().str();
    }
#endif // LLVM > 3.6
}

///
/// constructed_only  Search the criteria in DG's constructed functions
///
static std::vector<SlicingCriteriaSet> getSlicingCriteriaInstructions(
        llvm::Module &M, const std::string &slicingCriteria,
        bool criteria_are_next_instr, LLVMPointerAnalysis *pta = nullptr,
        bool constructed_only = false) {
    std::vector<std::string> criteria = splitList(slicingCriteria, ';');
    assert(!criteria.empty() && "Did not get slicing criteria");

    std::vector<SlicingCriteriaSet> result;
    std::set<const llvm::Value *> secondaryToAll;

    // map the criteria to instructions
    for (const auto &crit : criteria) {
        if (crit.empty())
            continue;

        result.emplace_back();

        auto primsec = splitList(crit, '|');
        if (primsec.size() > 2) {
            llvm::errs() << "WARNING: Only one | in SC supported, ignoring the "
                            "rest\n";
        }
        assert(primsec.size() >= 1 && "Invalid criterium");
        auto &SC = result.back();
        // do we have some criterion of the form |X?
        // I.e., only secondary SC? It means that that should
        // be added to every primary SC
        bool ssctoall = primsec[0].empty() && primsec.size() > 1;
        if (!primsec[0].empty()) {
            getCriteriaInstructions(M, pta, primsec[0], SC.primary,
                                    constructed_only);
        }

        if (!SC.primary.empty()) {
            llvm::errs() << "SC: Matched '" << primsec[0] << "' to: \n";
            for (const auto *val : SC.primary) {
                llvm::errs() << "  " << *val << "\n";
            }

            if (criteria_are_next_instr) {
                // the given (primary) criteria are just markers for the
                // next instruction, so map the criteria to
                // the next instructions
                auto newset = mapToNextInstr(SC.primary);
                SC.primary.swap(newset);

                for (const auto *val : SC.primary) {
                    llvm::errs() << "  SC (next): " << *val << "\n";
                }
            }
        }

        if ((!SC.primary.empty() || ssctoall) && primsec.size() > 1) {
            getCriteriaInstructions(M, pta, primsec[1], SC.secondary,
                                    constructed_only);

            if (!SC.secondary.empty()) {
                llvm::errs() << "SC: Matched '" << primsec[1]
                             << "' (secondary) to: \n";
                for (const auto *val : SC.secondary) {
                    llvm::errs() << "  " << *val << "\n";
                }
            }

            if (ssctoall) {
                secondaryToAll.insert(SC.secondary.begin(), SC.secondary.end());
            }
        }
    }

    if (!secondaryToAll.empty()) {
        for (auto &SC : result) {
            if (SC.primary.empty())
                continue;
            SC.secondary.insert(secondaryToAll.begin(), secondaryToAll.end());
        }
    }

    return result;
}

void mapInstrsToNodes(LLVMDependenceGraph &dg,
                      const std::set<const llvm::Value *> &vals,
                      std::set<LLVMNode *> &result) {
    const auto &funs = getConstructedFunctions();
    for (const auto *val : vals) {
        if (llvm::isa<llvm::GlobalVariable>(val)) {
            auto *G = dg.getGlobalNode(const_cast<llvm::Value *>(val));
            assert(G);
            result.insert(G);
        } else if (const auto *I = llvm::dyn_cast<llvm::Instruction>(val)) {
            auto *fun =
                    const_cast<llvm::Function *>(I->getParent()->getParent());
            auto it = funs.find(fun);
            assert(it != funs.end() && "Do not have DG for a fun");
            LLVMNode *nd = it->second->getNode(const_cast<llvm::Value *>(val));
            assert(nd);
            result.insert(nd);
        } else {
            assert(false && "Unhandled slicing criterion");
        }
    }
}

std::vector<const llvm::Function *>
getCalledFunctions(LLVMDependenceGraph &dg, const llvm::CallInst *C) {
    auto *fun = C->getCalledFunction();
    if (fun)
        return {fun};

#if LLVM_VERSION_MAJOR >= 8
    auto *V = C->getCalledOperand()->stripPointerCasts();
#else
    auto *V = C->getCalledValue()->stripPointerCasts();
#endif

    return dg::getCalledFunctions(V, dg.getPTA());
}

// WHOO, this is horrible. Refactor it into a class...
void processBlock(LLVMDependenceGraph &dg, const llvm::BasicBlock *block,
                  std::set<const llvm::BasicBlock *> &visited,
                  ADT::QueueLIFO<const llvm::BasicBlock *> &queue,
                  const std::set<const llvm::Value *> &secondary,
                  std::set<const llvm::Value *> &result,
                  const llvm::Instruction *till = nullptr) {
    for (const auto &I : *block) {
        if (till == &I)
            break;

        if (secondary.count(&I) > 0) {
            result.insert(&I);
        }

        if (const auto *C = llvm::dyn_cast<llvm::CallInst>(&I)) {
            // queue ret blocks from the called functions
            for (const auto *fun : getCalledFunctions(dg, C)) {
                for (const auto &blk : *fun) {
                    if (llvm::isa<llvm::ReturnInst>(blk.getTerminator())) {
                        if (visited.insert(&blk).second)
                            queue.push(&blk);
                    }
                }
            }
        }
    }
}

// mark nodes that are going to be in the slice
std::set<const llvm::Value *>
findSecondarySlicingCriteria(LLVMDependenceGraph &dg,
                             const std::set<const llvm::Value *> &primary,
                             const std::set<const llvm::Value *> &secondary) {
    std::set<const llvm::Value *> result;
    std::set<const llvm::BasicBlock *> visited;
    ADT::QueueLIFO<const llvm::BasicBlock *> queue;

    for (const auto *c : primary) {
        const auto *I = llvm::dyn_cast<llvm::Instruction>(c);
        // the criterion instr may be a global variable and in that
        // case it has no basic block (but also no predecessors,
        // so we can skip it)
        if (!I)
            continue;

        processBlock(dg, I->getParent(), visited, queue, secondary, result, I);

        // queue local predecessors
        for (const auto *pred : llvm::predecessors(I->getParent())) {
            if (visited.insert(pred).second)
                queue.push(pred);
        }
    }

    // get basic blocks
    while (!queue.empty()) {
        const auto *cur = queue.pop();

        processBlock(dg, cur, visited, queue, secondary, result);

        // queue local predecessors
        for (const auto *pred : llvm::predecessors(cur)) {
            if (visited.insert(pred).second)
                queue.push(pred);
        }
    }

    return result;
}

bool getSlicingCriteriaNodes(LLVMDependenceGraph &dg,
                             const std::string &slicingCriteria,
                             std::set<LLVMNode *> &criteria_nodes,
                             bool criteria_are_next_instr) {
    initDebugInfo(dg);

    auto crits =
            getSlicingCriteriaInstructions(*dg.getModule(), slicingCriteria,
                                           criteria_are_next_instr, dg.getPTA(),
                                           /* constructed only */ true);
    if (crits.empty()) {
        return true; // no criteria found
    }

    for (auto &SC : crits) {
        if (SC.primary.empty()) {
            continue;
        }

        mapInstrsToNodes(dg, SC.primary, criteria_nodes);

        if (SC.secondary.empty()) {
            continue;
        }
        auto ssc = findSecondarySlicingCriteria(dg, SC.primary, SC.secondary);
        mapInstrsToNodes(dg, ssc, criteria_nodes);
    }

    return true;
}

namespace legacy {

static bool
instMatchesCrit(LLVMDependenceGraph &dg, const llvm::Instruction &I,
                const std::vector<std::pair<int, std::string>> &parsedCrit) {
    for (const auto &c : parsedCrit) {
        const auto &Loc = I.getDebugLoc();
#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
        if (Loc.getLine() <= 0) {
#else
        if (!Loc) {
#endif
            continue;
        }

        if (static_cast<int>(Loc.getLine()) != c.first)
            continue;

        if (instIsCallOf(I, c.second, dg.getPTA())) {
            llvm::errs() << "Matched line " << c.first << " with call of "
                         << c.second << " to:\n"
                         << I << "\n";
            return true;
        } // else fall through to check the vars

        if (usesTheVariable(I, c.second, dg.getPTA())) {
            llvm::errs() << "Matched line " << c.first << " with variable "
                         << c.second << " to:\n"
                         << I << "\n";
            return true;
        }
    }

    return false;
}

static bool
globalMatchesCrit(const llvm::GlobalVariable &G,
                  const std::vector<std::pair<int, std::string>> &parsedCrit) {
    for (const auto &c : parsedCrit) {
        if (c.first != -1)
            continue;
        if (c.second == G.getName().str()) {
            llvm::errs() << "Matched global variable " << c.second << " to:\n"
                         << G << "\n";
            return true;
        }
    }

    return false;
}

static void getLineCriteriaNodes(LLVMDependenceGraph &dg,
                                 std::vector<std::string> &criteria,
                                 std::set<LLVMNode *> &nodes) {
    assert(!criteria.empty() && "No criteria given");

    std::vector<std::pair<int, std::string>> parsedCrit;
    for (auto &crit : criteria) {
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
            llvm::errs()
                    << "Invalid line: '" << parts[0] << "'. "
                    << "Needs to be a number or empty for global variables.\n";
        }
    }

    assert(!parsedCrit.empty() && "Failed parsing criteria");

    initDebugInfo(dg);

    // try match globals
    for (auto &G : dg.getModule()->globals()) {
        if (globalMatchesCrit(G, parsedCrit)) {
            LLVMNode *nd = dg.getGlobalNode(&G);
            assert(nd);
            nodes.insert(nd);
        }
    }

    // we do not have any mapping, we will not match anything
    if (valuesToVariables.empty()) {
        return;
    }

    // map line criteria to nodes
    for (const auto &it : getConstructedFunctions()) {
        for (auto &I :
             llvm::instructions(*llvm::cast<llvm::Function>(it.first))) {
            if (instMatchesCrit(dg, I, parsedCrit)) {
                LLVMNode *nd = it.second->getNode(&I);
                assert(nd);
                nodes.insert(nd);
            }
        }
    }
}

static std::set<LLVMNode *>
_mapToNextInstr(LLVMDependenceGraph & /*unused*/,
                const std::set<LLVMNode *> &callsites) {
    std::set<LLVMNode *> nodes;

    for (LLVMNode *cs : callsites) {
        llvm::Instruction *I =
                llvm::dyn_cast<llvm::Instruction>(cs->getValue());
        assert(I && "Callsite is not an instruction");
        llvm::Instruction *succ = I->getNextNode();
        if (!succ) {
            llvm::errs() << *I << "has no successor that could be criterion\n";
            // abort for now
            abort();
        }

        LLVMDependenceGraph *local_dg = cs->getDG();
        LLVMNode *node = local_dg->getNode(succ);
        assert(node && "DG does not have such node");
        nodes.insert(node);
    }

    return nodes;
}

static std::set<LLVMNode *>
getPrimarySlicingCriteriaNodes(LLVMDependenceGraph &dg,
                               const std::string &slicingCriteria,
                               bool criteria_are_next_instr) {
    std::set<LLVMNode *> nodes;
    std::vector<std::string> criteria = splitList(slicingCriteria);
    assert(!criteria.empty() && "Did not get slicing criteria");

    std::vector<std::string> line_criteria;
    std::vector<std::string> node_criteria;
    std::tie(line_criteria, node_criteria) =
            splitStringVector(criteria, [](std::string &s) -> bool {
                return s.find(':') != std::string::npos;
            });

    // if user wants to slice with respect to the return of main,
    // insert the ret instructions to the nodes.
    for (const auto &c : node_criteria) {
        if (c == "ret") {
            LLVMNode *exit = dg.getExit();
            // We could insert just the exit node, but this way we will
            // get annotations to the functions.
            for (auto it = exit->rev_control_begin(),
                      et = exit->rev_control_end();
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

    if (criteria_are_next_instr && !nodes.empty()) {
        // the given criteria are just markers for the
        // next instruction, so map the criteria to
        // the next instructions
        auto mappedNodes = _mapToNextInstr(dg, nodes);
        nodes.swap(mappedNodes);
    }

    return nodes;
}

static std::pair<std::set<std::string>, std::set<std::string>>
parseSecondarySlicingCriteria(const std::string &slicingCriteria) {
    std::vector<std::string> criteria = splitList(slicingCriteria);

    std::set<std::string> control_criteria;
    std::set<std::string> data_criteria;

    // if user wants to slice with respect to the return of main,
    // insert the ret instructions to the nodes.
    for (const auto &c : criteria) {
        auto s = c.size();
        if (s > 2 && c[s - 2] == '(' && c[s - 1] == ')')
            data_criteria.insert(c.substr(0, s - 2));
        else
            control_criteria.insert(c);
    }

    return {control_criteria, data_criteria};
}

// FIXME: copied from LLVMDependenceGraph.cpp, do not duplicate the code
static bool isCallTo(LLVMNode *callNode, const std::set<std::string> &names) {
    using namespace llvm;

    if (!isa<llvm::CallInst>(callNode->getValue())) {
        return false;
    }

    // if the function is undefined, it has no subgraphs,
    // but is not called via function pointer
    if (!callNode->hasSubgraphs()) {
        const CallInst *callInst = cast<CallInst>(callNode->getValue());
#if LLVM_VERSION_MAJOR >= 8
        const Value *calledValue = callInst->getCalledOperand();
#else
        const Value *calledValue = callInst->getCalledValue();
#endif
        const Function *func =
                dyn_cast<Function>(calledValue->stripPointerCasts());
        // in the case we haven't run points-to analysis
        if (!func)
            return false;

        return array_match(func->getName(), names);
    } // simply iterate over the subgraphs, get the entry node
    // and check it
    for (LLVMDependenceGraph *dg : callNode->getSubgraphs()) {
        LLVMNode *entry = dg->getEntry();
        assert(entry && "No entry node in graph");

        const Function *func =
                cast<Function>(entry->getValue()->stripPointerCasts());
        return array_match(func->getName(), names);
    }

    return false;
}

static inline void
checkSecondarySlicingCrit(std::set<LLVMNode *> &criteria_nodes,
                          const std::set<std::string> &secondaryControlCriteria,
                          const std::set<std::string> &secondaryDataCriteria,
                          LLVMNode *nd) {
    if (isCallTo(nd, secondaryControlCriteria))
        criteria_nodes.insert(nd);
    if (isCallTo(nd, secondaryDataCriteria)) {
        llvm::errs()
                << "WARNING: Found possible data secondary slicing criterion: "
                << *nd->getValue() << "\n";
        llvm::errs() << "This is not fully supported, so adding to be sound\n";
        criteria_nodes.insert(nd);
    }
}

// mark nodes that are going to be in the slice
static bool findSecondarySlicingCriteria(
        std::set<LLVMNode *> &criteria_nodes,
        const std::set<std::string> &secondaryControlCriteria,
        const std::set<std::string> &secondaryDataCriteria) {
    // FIXME: do this more efficiently (and use the new DFS class)
    std::set<LLVMBBlock *> visited;
    ADT::QueueLIFO<LLVMBBlock *> queue;
    auto tmp = criteria_nodes;
    for (const auto &c : tmp) {
        // the criteria may be a global variable and in that
        // case it has no basic block (but also no predecessors,
        // so we can skip it)
        if (!c->getBBlock())
            continue;

        queue.push(c->getBBlock());
        visited.insert(c->getBBlock());

        for (auto *nd : c->getBBlock()->getNodes()) {
            if (nd == c)
                break;

            if (nd->hasSubgraphs()) {
                // we search interprocedurally
                for (auto *dg : nd->getSubgraphs()) {
                    auto *exit = dg->getExitBB();
                    assert(exit && "No exit BB in a graph");
                    if (visited.insert(exit).second)
                        queue.push(exit);
                }
            }

            checkSecondarySlicingCrit(criteria_nodes, secondaryControlCriteria,
                                      secondaryDataCriteria, nd);
        }
    }

    // get basic blocks
    while (!queue.empty()) {
        auto *cur = queue.pop();
        for (auto *pred : cur->predecessors()) {
            for (auto *nd : pred->getNodes()) {
                if (nd->hasSubgraphs()) {
                    // we search interprocedurally
                    for (auto *dg : nd->getSubgraphs()) {
                        auto *exit = dg->getExitBB();
                        assert(exit && "No exit BB in a graph");
                        if (visited.insert(exit).second)
                            queue.push(exit);
                    }
                }

                checkSecondarySlicingCrit(criteria_nodes,
                                          secondaryControlCriteria,
                                          secondaryDataCriteria, nd);
            }
            if (visited.insert(pred).second)
                queue.push(pred);
        }
    }

    return true;
}

bool getSlicingCriteriaNodes(LLVMDependenceGraph &dg,
                             const std::string &slicingCriteria,
                             const std::string &secondarySlicingCriteria,
                             std::set<LLVMNode *> &criteria_nodes,
                             bool criteria_are_next_instr) {
    auto nodes = getPrimarySlicingCriteriaNodes(dg, slicingCriteria,
                                                criteria_are_next_instr);
    if (nodes.empty()) {
        return true; // no criteria found
    }

    criteria_nodes.swap(nodes);

    auto secondaryCriteria =
            parseSecondarySlicingCriteria(secondarySlicingCriteria);
    const auto &secondaryControlCriteria = secondaryCriteria.first;
    const auto &secondaryDataCriteria = secondaryCriteria.second;

    // mark nodes that are going to be in the slice
    if (!findSecondarySlicingCriteria(criteria_nodes, secondaryControlCriteria,
                                      secondaryDataCriteria)) {
        llvm::errs() << "Finding secondary slicing criteria nodes failed\n";
        return false;
    }

    return true;
}
} // namespace legacy

bool getSlicingCriteriaNodes(LLVMDependenceGraph &dg,
                             const std::string &slicingCriteria,
                             const std::string &legacySlicingCriteria,
                             const std::string &secondarySlicingCriteria,
                             std::set<LLVMNode *> &criteria_nodes,
                             bool criteria_are_next_instr) {
    if (!legacySlicingCriteria.empty()) {
        if (!::legacy::getSlicingCriteriaNodes(
                    dg, legacySlicingCriteria, secondarySlicingCriteria,
                    criteria_nodes, criteria_are_next_instr))
            return false;
    }

    if (!slicingCriteria.empty()) {
        if (!getSlicingCriteriaNodes(dg, slicingCriteria, criteria_nodes,
                                     criteria_are_next_instr))
            return false;
    }

    return true;
}

std::vector<const llvm::Value *>
getSlicingCriteriaValues(llvm::Module &M, const std::string &slicingCriteria,
                         const std::string &legacySlicingCriteria,
                         const std::string &legacySecondaryCriteria,
                         bool criteria_are_next_instr) {
    std::string criteria = slicingCriteria;
    if (legacySlicingCriteria != "") {
        if (slicingCriteria != "")
            criteria += ";";

        auto parts = splitList(legacySlicingCriteria, ':');
        if (parts.size() == 2) {
            if (legacySecondaryCriteria != "") {
                criteria += ";" + parts[0] + "#" + parts[1] + "|" +
                            legacySecondaryCriteria + "()";
            } else {
                criteria += parts[0] + "#" + parts[1];
            }
        } else if (parts.size() == 1) {
            if (legacySecondaryCriteria != "") {
                criteria += ";" + legacySlicingCriteria + "()|" +
                            legacySecondaryCriteria + "()";
            } else {
                criteria += legacySlicingCriteria + "()";
            }
        } else {
            llvm::errs() << "Unsupported criteria: " << legacySlicingCriteria
                         << "\n";
            return {};
        }
    }

    std::vector<const llvm::Value *> ret;
    auto C = getSlicingCriteriaInstructions(
            M, criteria, criteria_are_next_instr,
            /*pta = */ nullptr, /* constructed only */ false);
    for (auto &critset : C) {
        ret.insert(ret.end(), critset.primary.begin(), critset.primary.end());
        ret.insert(ret.end(), critset.secondary.begin(),
                   critset.secondary.end());
    }
    return ret;
}
