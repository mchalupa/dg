#include <set>
#include <vector>
#include <string>
#include <cassert>

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

#include "dg/tools/llvm-slicer-utils.h"
#include "dg/llvm/LLVMNode.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/ADT/Queue.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"

using namespace dg;

using llvm::errs;

// mapping of AllocaInst to the names of C variables
static std::map<const llvm::Value *, std::string> valuesToVariables;

static inline bool isNumber(const std::string& s) {
    assert(!s.empty());

    for (const auto c : s)
        if (!isdigit(c))
            return false;

    return true;
}

static bool usesTheVariable(LLVMDependenceGraph& dg,
                            const llvm::Instruction& I,
                            const std::string& var,
                            bool isglobal = false) {
    if (!I.mayReadOrWriteMemory())
        return false;

    auto memacc = dg.getPTA()->getAccessedMemory(&I);
    if (memacc.first) {
        // PTA has no information, it may be a definition of the variable,
        // we do not know
        llvm::errs() << "WARNING: matched due to a lack of information: "
                     << I << "\n";
        return true;
    }

    for (const auto& region : memacc.second) {
        if (isglobal &&
            !llvm::isa<llvm::GlobalVariable>(region.pointer.value)) {
            continue;
        }
        auto name = valuesToVariables.find(region.pointer.value);
        if (name != valuesToVariables.end()) {
            if (name->second == var) {
                return true;
            }
        }
    }

    return false;
}

static bool instIsCallOf(LLVMDependenceGraph& dg,
                         const llvm::Instruction& I,
                         const std::string& name) {
    auto *C = llvm::dyn_cast<llvm::CallInst>(&I);
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

    auto pts = dg.getPTA()->getLLVMPointsTo(V);
    if (pts.empty()) {
        return true; // may be, we do not know...
    }

    for (const auto& ptr : pts) {
        fun = llvm::dyn_cast<llvm::Function>(ptr.value);
        if (!fun)
            continue;
        if (name == fun->getName().str())
            return true;
    }

    return false;
}

static bool fileMatch(const std::string& file,
                      const llvm::Instruction& I) {
#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 7
    const auto *F = I.getParent()->getParent();
    const auto *subprog = llvm::cast<llvm::DISubprogram>(F->getMetadata(llvm::LLVMContext::MD_dbg));
#else
    const auto *subprog = I.getFunction()->getSubprogram();
#endif
    return subprog->getFile()->getFilename() == file;
}

static bool fileMatch(const std::string& file,
                      const llvm::GlobalVariable& G) {
#if LLVM_VERSION_MAJOR < 4
    return true;
#else
    llvm::SmallVector<llvm::DIGlobalVariableExpression *, 2> GVs;
    G.getDebugInfo(GVs);
    bool has_match = false;
    for (auto GV : GVs) {
        auto *var = GV->getVariable();
        if (var->getFile()->getFilename() == file) {
            has_match = true;
            break;
        }
    }
    return has_match;
#endif
}

static bool instMatchesCrit(LLVMDependenceGraph& dg,
                            const llvm::Instruction& I,
                            const std::string& fun,
                            unsigned line,
                            const std::string& obj) {

    // function match?
    if (fun != "" &&
        I.getParent()->getParent()->getName().str() != fun)
        return false;

    // line match?
    if (line > 0) {
     auto& Loc = I.getDebugLoc();
#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
        if (Loc.getLine() != line)
#else
        if (!Loc || line != Loc.getLine())
#endif
        {
            return false;
        }
    }

    if (obj == "") {
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
                            "at the same time: " << obj << "\n";
        }
        return false;
    }

     // obj match?
     if (!isvar && instIsCallOf(dg, I, objname)) {
         return true;
     } // else fall through to check the vars

     if (!isfunc && usesTheVariable(dg, I, objname, isglobal)) {
         return true;
     }

    return false;
}

static bool globalMatchesCrit(const llvm::GlobalVariable& G,
                              unsigned line,
                              const std::string& obj) {
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
        for (auto GV : GVs) {
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

static unsigned parseLine(const std::vector<std::string>& parts) {
    unsigned idx = -1;
    switch (parts.size()) {
        case 2: idx = 0; break;
        case 3: idx = 1; break;
        case 4: idx = 2; break;
        default: return 0;
    }

    assert(idx == 0 || idx <= 2);
    assert(idx < parts.size());

    if (parts[idx] == "" || parts[idx] == "*")
        return 0; // any line

    // will we support multiple lines separated by comma?
    if (!isNumber(parts[idx])) {
        llvm::errs() << "ERROR: invalid line number: " << parts[idx] << "\n";
        return 0;
    }

    return atoi(parts[idx].c_str());
}

static std::string parseFile(const std::vector<std::string>& parts) {
    if (parts.size() == 4)
        return parts[0];
    return "";
}

static std::string parseFun(const std::vector<std::string>& parts) {
    switch (parts.size()) {
        case 4: return parts[1];
        case 3: return parts[0];
        default: return "";
    }
}

static std::string parseObj(const std::vector<std::string>& parts) {
    assert(parts.size() > 0);
    return parts[parts.size() - 1];
}

static void getCriteriaInstructions(LLVMDependenceGraph& dg,
                                    const std::string& criterion,
                                    std::set<const llvm::Value *>& result) {
    assert(!criterion.empty() && "No criteria given");

    auto parts = splitList(criterion, '#');
    if (parts.size() > 4 || parts.size() < 1) {
        llvm::errs() << "WARNING: ignoring invalid slicing criterion: "
                     << criterion << "\n";
        return;
    }

    unsigned line = parseLine(parts);
    auto fun = parseFun(parts);
    auto obj = parseObj(parts);
    auto file= parseFile(parts);

    if (fun != "" && obj == "" && line == 0) {
        llvm::errs() << "WARNING: ignoring invalid slicing criterion: "
                     << criterion << "\n";
        return;
    }

    // try match globals
    if (fun == "") {
        for (auto& G : dg.getModule()->globals()) {
            if (file != "" && !fileMatch(file, G))
                continue;
            if (globalMatchesCrit(G, line, obj)) {
                result.insert(&G);
            }
        }
    }

    // map line criteria to nodes
    for (auto& it : getConstructedFunctions()) {
        for (auto& I : llvm::instructions(*llvm::cast<llvm::Function>(it.first))) {
            if (file != "" && !fileMatch(file, I))
                continue;

            if (instMatchesCrit(dg, I, fun, line, obj)) {
                result.insert(&I);
            }
        }
    }
}

struct SlicingCriteriaSet {
    std::set<const llvm::Value *> primary;
    std::set<const llvm::Value *> secondary;

    SlicingCriteriaSet() = default;
    SlicingCriteriaSet(SlicingCriteriaSet&&) = default;
};

static std::set<const llvm::Value *>
mapToNextInstr(const std::set<const::llvm::Value *>& vals) {
    std::set<const llvm::Value *> newset;
    for (const auto *val : vals) {
        auto *I = llvm::dyn_cast<llvm::Instruction>(val);
        I = I ? I->getNextNode() : nullptr;
        if (!I) {
            llvm::errs() << "WARNING: unable to get next instr for "
                         << *val << "\n";
            continue;
        }
        newset.insert(I);
    }
    return newset;
}

static void initDebugInfo(LLVMDependenceGraph& dg) {
    if (!valuesToVariables.empty())
        return;

#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
    llvm::errs() << "WARNING: Variables names matching is not supported for LLVM older than 3.7\n";
    llvm::errs() << "WARNING: The slicing criteria with variables names will not work\n";
#else // LLVM >= 3.8
#if (LLVM_VERSION_MAJOR < 4)
    llvm::errs() << "WARNING: Function/global names matching is not supported for LLVM older than 4\n";
    llvm::errs() << "WARNING: The slicing criteria with variables names will not work well\n";
#endif
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

    bool no_dbg = valuesToVariables.empty();
    if (no_dbg) {
        llvm::errs() << "No debugging information found in program, "
                     << "slicing criteria with lines and variables will work\n"
                     << "only for global variables. "
                     << "You can still use the criteria based on call sites ;)\n";
    }

    for (const auto& GV : dg.getModule()->globals()) {
        valuesToVariables[&GV] = GV.getName().str();
    }
#endif // LLVM > 3.6
}


static std::vector<SlicingCriteriaSet>
getSlicingCriteriaInstructions(LLVMDependenceGraph& dg,
                               const std::string& slicingCriteria,
                               bool criteria_are_next_instr) {

    std::vector<std::string> criteria = splitList(slicingCriteria, ';');
    assert(!criteria.empty() && "Did not get slicing criteria");

    std::vector<SlicingCriteriaSet> result;
    std::set<const llvm::Value *> secondaryToAll;

    // map the criteria to instructions
    for (const auto& crit : criteria) {
        if (crit == "")
            continue;

        result.emplace_back();

        auto primsec = splitList(crit, '|');
        if (primsec.size() > 2) {
            llvm::errs() << "WARNING: Only one | in SC supported, ignoring the rest\n";
        }
        assert(primsec.size() >= 1 && "Invalid criterium");
        auto& SC = result.back();
        // do we have some criterion of the form |X?
        // I.e., only secondary SC? It means that that should
        // be added to every primary SC
        bool ssctoall = primsec[0].empty() && primsec.size() > 1;
        if (!primsec[0].empty()) {
            getCriteriaInstructions(dg, primsec[0], SC.primary);
        }

        if (!SC.primary.empty()) {
            llvm::errs() << "SC: Matched '" << primsec[0] << "' to: \n";
            for (auto *val : SC.primary) {
                llvm::errs() << "  " << *val << "\n";
            }

            if (criteria_are_next_instr) {
                // the given (primary) criteria are just markers for the
                // next instruction, so map the criteria to
                // the next instructions
                auto newset = mapToNextInstr(SC.primary);
                SC.primary.swap(newset);

                for (auto *val : SC.primary) {
                    llvm::errs() << "  SC (next): " << *val << "\n";
                }
            }
        }

        if ((!SC.primary.empty() || ssctoall) && primsec.size() > 1) {
            getCriteriaInstructions(dg, primsec[1], SC.secondary);

            if (!SC.secondary.empty()) {
                llvm::errs() << "SC: Matched '" << primsec[1]
                             << "' (secondary) to: \n";
                for (auto *val : SC.secondary) {
                    llvm::errs() << "  " << *val << "\n";
                }
            }

            if (ssctoall) {
                secondaryToAll.insert(SC.secondary.begin(),
                                      SC.secondary.end());
            }
        }
    }

    if (!secondaryToAll.empty()) {
        for (auto& SC : result) {
            if (SC.primary.empty())
                continue;
            SC.secondary.insert(secondaryToAll.begin(),
                                secondaryToAll.end());
        }
    }

    return result;
}

void mapInstrsToNodes(LLVMDependenceGraph& dg,
                      const std::set<const llvm::Value *>& vals,
                      std::set<LLVMNode *>& result) {
    auto &funs = getConstructedFunctions();
    for (auto *val : vals) {
        if (llvm::isa<llvm::GlobalVariable>(val)) {
            auto *G = dg.getGlobalNode(const_cast<llvm::Value*>(val));
            assert(G);
            result.insert(G);
        } else if (auto *I = llvm::dyn_cast<llvm::Instruction>(val)) {
            auto *fun = const_cast<llvm::Function*>(I->getParent()->getParent());
            auto it = funs.find(fun);
            assert(it != funs.end() && "Do not have DG for a fun");
            LLVMNode *nd = it->second->getNode(const_cast<llvm::Value*>(val));
            assert(nd);
            result.insert(nd);
        } else {
            assert(false && "Unhandled slicing criterion");
        }
    }
}

std::vector<const llvm::Function *>
getCalledFunctions(LLVMDependenceGraph& dg, const llvm::CallInst *C) {
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
void processBlock(LLVMDependenceGraph& dg,
                  const llvm::BasicBlock *block,
                  std::set<const llvm::BasicBlock *>& visited,
                  ADT::QueueLIFO<const llvm::BasicBlock*>& queue,
                  const std::set<const llvm::Value *>& secondary,
                  std::set<const llvm::Value *>& result,
                  const llvm::Instruction *till = nullptr) {

    for (auto& I : *block) {
        if (till == &I)
            break;

        if (secondary.count(&I) > 0) {
            result.insert(&I);
        }

        if (auto *C = llvm::dyn_cast<llvm::CallInst>(&I)) {
            // queue ret blocks from the called functions
            for (auto *fun : getCalledFunctions(dg, C)) {
                for (auto& blk : *fun) {
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
findSecondarySlicingCriteria(LLVMDependenceGraph& dg,
                             const std::set<const llvm::Value *>& primary,
                             const std::set<const llvm::Value *>& secondary) {

    std::set<const llvm::Value *> result;

    std::set<const llvm::BasicBlock *> visited;
    ADT::QueueLIFO<const llvm::BasicBlock *> queue;
    for (auto *c : primary) {
        auto *I = llvm::dyn_cast<llvm::Instruction>(c);
        // the criterion instr may be a global variable and in that
        // case it has no basic block (but also no predecessors,
        // so we can skip it)
        if (!I)
            continue;

        // FIXME: don't fuck with the type system... rewrite the whole code...
        processBlock(dg, I->getParent(),
                     visited, queue, secondary, result, I);

        // queue local predecessors
        for (auto *pred : llvm::predecessors(I->getParent())) {
            if (visited.insert(pred).second)
                queue.push(pred);
        }
    }

    // get basic blocks
    while (!queue.empty()) {
        auto *cur = queue.pop();

        processBlock(dg, cur, visited, queue, secondary, result);

        // queue local predecessors
        for (auto pred : llvm::predecessors(cur)) {
            if (visited.insert(pred).second)
                queue.push(pred);
        }
    }

    return result;
}



bool getSlicingCriteriaNodes(LLVMDependenceGraph& dg,
                             const std::string& slicingCriteria,
                             std::set<LLVMNode *>& criteria_nodes,
                             bool criteria_are_next_instr) {

    initDebugInfo(dg);

    auto crits = getSlicingCriteriaInstructions(dg,
                                                slicingCriteria,
                                                criteria_are_next_instr);
    if (crits.empty()) {
        return true; // no criteria found
    }

    for (auto& SC : crits) {
        if (SC.primary.empty()) {
            continue;
        }

        mapInstrsToNodes(dg, SC.primary, criteria_nodes);

        if (SC.secondary.empty()) {
            continue;
        }
        auto ssc = findSecondarySlicingCriteria(dg, SC.primary,
                                                SC.secondary);
        mapInstrsToNodes(dg, ssc, criteria_nodes);
    }

    return true;
}



namespace legacy {

static bool instMatchesCrit(LLVMDependenceGraph& dg,
                            const llvm::Instruction& I,
                            const std::vector<std::pair<int, std::string>>& parsedCrit)
{
    for (const auto& c : parsedCrit) {
        auto& Loc = I.getDebugLoc();
#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
        if (Loc.getLine() <= 0) {
#else
        if (!Loc) {
#endif
            continue;
        }

        if (static_cast<int>(Loc.getLine()) != c.first)
            continue;

        if (instIsCallOf(dg, I, c.second)) {
            llvm::errs() << "Matched line " << c.first << " with call of "
                         << c.second << " to:\n" << I << "\n";
            return true;
        } // else fall through to check the vars

        if (usesTheVariable(dg, I, c.second)) {
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

    initDebugInfo(dg);

    // try match globals
    for (auto& G : dg.getModule()->globals()) {
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
    for (auto& it : getConstructedFunctions()) {
        for (auto& I : llvm::instructions(*llvm::cast<llvm::Function>(it.first))) {
            if (instMatchesCrit(dg, I, parsedCrit)) {
                LLVMNode *nd = it.second->getNode(&I);
                assert(nd);
                nodes.insert(nd);
            }
        }
    }
}

static std::set<LLVMNode *> _mapToNextInstr(LLVMDependenceGraph&,
                                            const std::set<LLVMNode *>& callsites) {
    std::set<LLVMNode *> nodes;

    for (LLVMNode *cs: callsites) {
        llvm::Instruction *I = llvm::dyn_cast<llvm::Instruction>(cs->getValue());
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

static
std::set<LLVMNode *> getPrimarySlicingCriteriaNodes(LLVMDependenceGraph& dg,
                                                    const std::string& slicingCriteria,
                                                    bool criteria_are_next_instr) {
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
parseSecondarySlicingCriteria(const std::string& slicingCriteria)
{
    std::vector<std::string> criteria = splitList(slicingCriteria);

    std::set<std::string> control_criteria;
    std::set<std::string> data_criteria;

    // if user wants to slice with respect to the return of main,
    // insert the ret instructions to the nodes.
    for (const auto& c : criteria) {
        auto s = c.size();
        if (s > 2 && c[s - 2] == '(' && c[s - 1] == ')')
            data_criteria.insert(c.substr(0, s - 2));
        else
            control_criteria.insert(c);
    }

    return {control_criteria, data_criteria};
}

// FIXME: copied from LLVMDependenceGraph.cpp, do not duplicate the code
static bool isCallTo(LLVMNode *callNode, const std::set<std::string>& names)
{
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
        const Function *func = dyn_cast<Function>(calledValue->stripPointerCasts());
        // in the case we haven't run points-to analysis
        if (!func)
            return false;

        return array_match(func->getName(), names);
    } else {
        // simply iterate over the subgraphs, get the entry node
        // and check it
        for (LLVMDependenceGraph *dg : callNode->getSubgraphs()) {
            LLVMNode *entry = dg->getEntry();
            assert(entry && "No entry node in graph");

            const Function *func
                = cast<Function>(entry->getValue()->stripPointerCasts());
            return array_match(func->getName(), names);
        }
    }

    return false;
}

static inline
void checkSecondarySlicingCrit(std::set<LLVMNode *>& criteria_nodes,
                               const std::set<std::string>& secondaryControlCriteria,
                               const std::set<std::string>& secondaryDataCriteria,
                               LLVMNode *nd) {
    if (isCallTo(nd, secondaryControlCriteria))
        criteria_nodes.insert(nd);
    if (isCallTo(nd, secondaryDataCriteria)) {
        llvm::errs() << "WARNING: Found possible data secondary slicing criterion: "
                    << *nd->getValue() << "\n";
        llvm::errs() << "This is not fully supported, so adding to be sound\n";
        criteria_nodes.insert(nd);
    }
}


// mark nodes that are going to be in the slice
static
bool findSecondarySlicingCriteria(std::set<LLVMNode *>& criteria_nodes,
                                  const std::set<std::string>& secondaryControlCriteria,
                                  const std::set<std::string>& secondaryDataCriteria)
{
    // FIXME: do this more efficiently (and use the new DFS class)
    std::set<LLVMBBlock *> visited;
    ADT::QueueLIFO<LLVMBBlock *> queue;
    auto tmp = criteria_nodes;
    for (auto&c : tmp) {
        // the criteria may be a global variable and in that
        // case it has no basic block (but also no predecessors,
        // so we can skip it)
        if (!c->getBBlock())
            continue;

        queue.push(c->getBBlock());
        visited.insert(c->getBBlock());

        for (auto nd : c->getBBlock()->getNodes()) {
            if (nd == c)
                break;

            if (nd->hasSubgraphs()) {
                // we search interprocedurally
                for (auto dg : nd->getSubgraphs()) {
                    auto exit = dg->getExitBB();
                    assert(exit && "No exit BB in a graph");
                    if (visited.insert(exit).second)
                        queue.push(exit);
                }
            }

            checkSecondarySlicingCrit(criteria_nodes,
                                      secondaryControlCriteria,
                                      secondaryDataCriteria, nd);
        }
    }

    // get basic blocks
    while (!queue.empty()) {
        auto cur = queue.pop();
        for (auto pred : cur->predecessors()) {
            for (auto nd : pred->getNodes()) {
                if (nd->hasSubgraphs()) {
                    // we search interprocedurally
                    for (auto dg : nd->getSubgraphs()) {
                        auto exit = dg->getExitBB();
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


bool getSlicingCriteriaNodes(LLVMDependenceGraph& dg,
                             const std::string& slicingCriteria,
                             const std::string& secondarySlicingCriteria,
                             std::set<LLVMNode *>& criteria_nodes,
                             bool criteria_are_next_instr) {

    auto nodes = getPrimarySlicingCriteriaNodes(dg,
                                                slicingCriteria,
                                                criteria_are_next_instr);
    if (nodes.empty()) {
        return true; // no criteria found
    }

    criteria_nodes.swap(nodes);

    auto secondaryCriteria
        = parseSecondarySlicingCriteria(secondarySlicingCriteria);
    const auto& secondaryControlCriteria = secondaryCriteria.first;
    const auto& secondaryDataCriteria = secondaryCriteria.second;

    // mark nodes that are going to be in the slice
    if (!findSecondarySlicingCriteria(criteria_nodes,
                                      secondaryControlCriteria,
                                      secondaryDataCriteria)) {
        llvm::errs() << "Finding secondary slicing criteria nodes failed\n";
        return false;
    }

    return true;
}
} // namespace legacy


bool getSlicingCriteriaNodes(LLVMDependenceGraph& dg,
                             const std::string& slicingCriteria,
                             const std::string& legacySlicingCriteria,
                             const std::string& secondarySlicingCriteria,
                             std::set<LLVMNode *>& criteria_nodes,
                             bool criteria_are_next_instr) {
    if (!legacySlicingCriteria.empty()) {
        if (!::legacy::getSlicingCriteriaNodes(dg, legacySlicingCriteria,
                                               secondarySlicingCriteria,
                                               criteria_nodes,
                                               criteria_are_next_instr))
            return false;
    }

    if (!slicingCriteria.empty()) {
        if (!getSlicingCriteriaNodes(dg, slicingCriteria,
                                     criteria_nodes,
                                     criteria_are_next_instr))
            return false;
    }

    return true;
}


