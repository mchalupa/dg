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

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/DebugInfoMetadata.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "llvm-slicer-utils.h"
#include "dg/llvm/LLVMNode.h"
#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/ADT/Queue.h"
#include "dg/llvm/analysis/PointsTo/PointerAnalysis.h"

using namespace dg;

using llvm::errs;

// mapping of AllocaInst to the names of C variables
static std::map<const llvm::Value *, std::string> valuesToVariables;

static bool usesTheVariable(LLVMDependenceGraph& dg,
                            const llvm::Value *v,
                            const std::string& var)
{
    auto pts = dg.getPTA()->getLLVMPointsTo(v);
    if (pts.empty() || pts.hasUnknown())
        return true; // it may be a definition of the variable, we do not know

    for (const auto& ptr : pts) {
        auto name = valuesToVariables.find(ptr.value);
        if (name != valuesToVariables.end()) {
            if (name->second == var)
                return true;
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
#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
        if (Loc.getLine() <= 0) {
#else
        if (!Loc) {
#endif
            continue;
        }

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

#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
    llvm::errs() << "WARNING: Variables names matching is not supported for LLVM older than 3.7\n";
    llvm::errs() << "WARNING: The slicing criteria with variables names will not work\n";
#else
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
        llvm::errs() << "No debugging information found in program,\n"
                     << "slicing criteria with lines and variables will work\n"
                     << "only for global variables.\n"
                     << "You can still use the criteria based on call sites ;)\n";
    }

    for (const auto& GV : dg.getModule()->globals()) {
        valuesToVariables[&GV] = GV.getName().str();
    }

    // try match globals
    for (auto& G : dg.getModule()->globals()) {
        if (globalMatchesCrit(G, parsedCrit)) {
            LLVMNode *nd = dg.getGlobalNode(&G);
            assert(nd);
            nodes.insert(nd);
        }
    }

    // we do not have any mapping, we will not match anything
    if (no_dbg) {
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
#endif // LLVM > 3.6
}

std::set<LLVMNode *> getSlicingCriteriaNodes(LLVMDependenceGraph& dg,
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

std::pair<std::set<std::string>, std::set<std::string>>
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
        const Value *calledValue = callInst->getCalledValue();
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

