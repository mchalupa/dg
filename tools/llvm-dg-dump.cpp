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

static void dump_to_dot(LLVMDGNode *n, std::ostream& out)
{
    const llvm::Value *val;

    /* do not draw multiple edges */
    if (n->getDFSrun() == 1)
        return;
    else
        n->setDFSrun(1);

    for (auto I = n->control_begin(), E = n->control_end();
         I != E; ++I)
        out << "\tNODE" << n->getValue() << " -> NODE" <<  (*I)->getValue() << "\n";
    for (auto I = n->dependence_begin(), E = n->dependence_end();
         I != E; ++I)
        out << "\tNODE" << n->getValue() << " -> NODE" <<  (*I)->getValue() << " [color=red]\n";
#if ENABLE_CFG
    for (auto I = n->succ_begin(), E = n->succ_end();
         I != E; ++I)
        out << "\tNODE" << n->getValue() << " -> NODE" <<  (*I)->getValue() << " [style=dotted]\n";
#endif /* ENABLE_CFG */

    if (n->getSubgraph()) {
        out << "\tNODE" << n->getValue() << " -> NODE" <<  n->getSubgraph()->getEntry()->getValue() << " [style=dashed]\n";
    }
}

static std::string& getValueName(const llvm::Value *val, std::string &str)
{
    llvm::raw_string_ostream s(str);

    if (llvm::isa<llvm::Function>(val))
        s << "ENTRY " << val->getName();
    else
        s << *val;

    return s.str();
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

    valName.clear();

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
            if (!llvm::isa<llvm::Function>(val))
                errs() << "WARN [" << dg << "]: Node is NULL for value: " << *I->first << "\n";

            continue;
        }

        val = n->getValue();

        print_to_dot(n->getSubgraph(), true);

        valName.clear();
        getValueName(val, valName);

        out << "\tNODE" << n->getValue() << " [label=\"" << valName << "\"";
        if (n->isLoopHeader())
            out << "style=\"filled\" fillcolor=\"gray\"";
        out << "];\n";
            //<<" (runid=" << n->getDFSrun() << ")\"];\n";
    }

    for (auto I = dg->begin(), E = dg->end(); I != E; ++I) {
        auto n = I->second;
        if (!n) {
            if (!llvm::isa<llvm::Function>(val))
                errs() << "WARN [" << dg << "]: Node is NULL for value: " << *I->first << "\n";

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
    const char *module, *ofile = NULL;

    if (argc == 3) {
        module = argv[1];
        ofile = argv[2];
        errs() << "Not supported yet\n";
        return 1;
    } else if (argc == 2) {
        module = argv[1];
    } else {
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
