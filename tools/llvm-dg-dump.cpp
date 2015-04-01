#include <assert.h>
#include <cstdio>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IRReader/IRReader.h>

#include <iostream>
#include <string>

#include "../src/LLVMDependenceGraph.h"

using namespace dg;
using llvm::errs;

static struct {
#if ENABLE_CFG
    bool printCFG;
    bool printRevCFG;
    bool printBB;
#endif

    bool printControlDep;
    bool printDataDep;
} OPTIONS;

static std::string& getValueName(const llvm::Value *val, std::string &str)
{
    llvm::raw_string_ostream s(str);

    str.clear();

    if (!val) {
        s << "[NULL]";
        return s.str();
    }

    if (llvm::isa<llvm::Function>(val))
        s << "ENTRY " << val->getName();
    else
        s << *val;

    return s.str();
}

static void dump_to_dot(LLVMDGNode *n, std::ostream& out)
{
    const llvm::Value *val;

    /* do not draw multiple edges */
    if (n->getDFSrun() == 1)
        return;
    else
        n->setDFSrun(1);

    if (OPTIONS.printControlDep)
        for (auto I = n->control_begin(), E = n->control_end();
             I != E; ++I)
            out << "\tNODE" << n << " -> NODE" <<  *I << "\n";

    if (OPTIONS.printDataDep)
        for (auto I = n->dependence_begin(), E = n->dependence_end();
             I != E; ++I)
            out << "\tNODE" << n << " -> NODE" <<  *I
                << " [color=red]\n";

#if ENABLE_CFG
    if (OPTIONS.printCFG)
        for (auto I = n->succ_begin(), E = n->succ_end();
             I != E; ++I)
            out << "\tNODE" << n << " -> NODE" <<  *I
                << " [style=dotted]\n";
    if (OPTIONS.printRevCFG)
        for (auto I = n->pred_begin(), E = n->pred_end();
             I != E; ++I)
            out << "\tNODE" << n << " -> NODE" <<  *I
                << " [style=dotted color=gray]\n";
    if (OPTIONS.printBB) {
        auto BB = n->getBasicBlock();
        // if this is BB header, print the edges
        if (BB && BB->getFirstNode() == n) {
            auto fn = BB->getFirstNode();
            for (auto pred : BB->predcessors()) {
                auto ln = pred->getLastNode();
                if (!ln) {
                    std::string valName;
                    getValueName(n->getValue(), valName);
                    errs() << "WARN: No last node for "
                           << valName << "\n";
                } else {
                    out << "\tNODE" << ln << " -> NODE" << fn
                        << " [style=dotted color=purple]\n";
                }
            }

            for (auto pred : BB->successors()) {
                auto ln = BB->getLastNode();
                auto fn = pred->getFirstNode();
                if (!ln) {
                    std::string valName;
                    getValueName(n->getValue(), valName);
                    errs() << "WARN: No last node for "
                           << valName << "\n";
                } else {
                    out << "\tNODE" << ln << " -> NODE" << fn
                        << " [style=dotted color=red]\n";
                }
            }
        }
    }
#endif // ENABLE_CFG

    if (n->getSubgraph()) {
        out << "\tNODE" << n << " -> NODE" <<  n->getSubgraph()->getEntry()
            << " [style=dashed label=\"call\"]\n";
    }
}

void print_to_dot(LLVMDependenceGraph *dg,
                  bool issubgraph = false,
                  const char *description = NULL)
{
    static unsigned subgraph_no = 0;
    const llvm::Value *val;
    std::string valName;
    std::ostream& out = std::cout;

    if (!dg)
        return;

    if (issubgraph) {
        out << "subgraph \"cluster_" << subgraph_no++;
    } else {
        out << "digraph \""<< description ? description : "DependencyGraph";
    }

    out << "\" {\n";

    for (auto I = dg->begin(), E = dg->end(); I != E; ++I)
    {
        auto n = I->second;
        if (!n) {
                getValueName(dg->getEntry()->getValue(), valName);
                errs() << "WARN [" << valName
                       << "]: Node is NULL for value: "
                       << *I->first << "\n";

            continue;
        }

        val = n->getValue();

        print_to_dot(n->getSubgraph(), true);
        LLVMDependenceGraph *params = n->getParameters();
        if (params) {
            print_to_dot(params, true);

            // add control edge from call-site to the parameters subgraph
            out << "\tNODE" << n << " -> NODE" <<  params->getEntry()
                << "[label=\"params\"]\n";
        }

        getValueName(val, valName);

        out << "\tNODE" << n << " [label=\"" << valName;

#ifdef ENABLE_CFG
        if (OPTIONS.printBB) {
            auto BB = n->getBasicBlock();
            if (!BB) {
                errs() << "WARN: No basic block for " << valName << "\n";
            } else {
                auto fn = BB->getFirstNode();
                if (!fn) {
                    errs() << "CRITICAL: No first value for "
                           << valName << "\n";
                } else {
                auto fval = fn->getValue();
                    if (!fval) {
                        errs() << "WARN: No value in first value for "
                               << valName << "\n";
                    } else {
                        getValueName(fval, valName);
                        out << "\\nBB: " << valName;
                        if (n == fn)
                            out << "\\nBB head (preds "
                                << BB->predcessorsNum() << ")";
                        if (BB->getLastNode() == n)
                            out << "\\nBB tail (succs "
                                << BB->successorsNum() << ")";
                    }
                }
            }
        }
#endif  // ENABLE_CFG

        for (auto d : n->getDefs()) {
            getValueName(d->getValue(), valName);
            out << "\\nDEF " << valName;
        }
        for (auto d : n->getPtrs()) {
            getValueName(d->getValue(), valName);
            out << "\\nPTR " << valName;
        }

        out << "\"];\n";
            //<<" (runid=" << n->getDFSrun() << ")\"];\n";
    }

    for (auto I = dg->begin(), E = dg->end(); I != E; ++I) {
        auto n = I->second;
        if (!n) {
                errs() << "WARN [" << dg
                       << "]: Node is NULL for value: "
                       << *I->first << "\n";

            continue;
        }

        dump_to_dot(I->second, out);
    }

    out << "}\n";
}

int main(int argc, char *argv[])
{
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    std::unique_ptr<llvm::Module> M;
    const char *module = NULL;

    // default
    OPTIONS.printControlDep = true;
    OPTIONS.printDataDep = true;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-no-control") == 0) {
            OPTIONS.printControlDep = false;
        } else if (strcmp(argv[i], "-no-data") == 0) {
            OPTIONS.printDataDep = false;

#if ENABLE_CFG
        } else if (strcmp(argv[i], "-bb") == 0) {
            OPTIONS.printBB = true;
        } else if (strcmp(argv[i], "-cfg") == 0) {
            OPTIONS.printCFG = true;
        } else if (strcmp(argv[i], "-cfgall") == 0) {
            OPTIONS.printCFG = true;
            OPTIONS.printRevCFG = true;
#endif // ENABLE_CFG

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

    print_to_dot(&d, false, "LLVM Dependence Graph");

    return 0;
}
