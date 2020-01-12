#ifndef DG_LLVM_SDG2DOT_H_
#define DG_LLVM_SDG2DOT_H_

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "llvm/IR/Instructions.h"

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 4))
#include "llvm/DebugInfo.h"     //DIScope
#else
#include "llvm/IR/DebugInfo.h"     //DIScope
#endif

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include <iostream>
#include <sstream>
#include <string>

#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"
#include "dg/SystemDependenceGraph/DGNodeCall.h"

namespace dg {
namespace llvmdg {

/*
FIXME: move to one file...
static std::ostream& operator<<(std::ostream& os, const analysis::Offset& off)
{
    if (off.offset == Offset::UNKNOWN)
        os << "UNKNOWN";
    else
        os << off.offset;

    return os;
}
*/

static std::ostream& printLLVMVal(std::ostream& os, const llvm::Value *val) {
    if (!val) {
        os << "(null)";
        return os;
    }

    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    if (llvm::isa<llvm::Function>(val)) {
        ro << "FUN " << val->getName();
    } else if (auto B = llvm::dyn_cast<llvm::BasicBlock>(val)) {
        ro << B->getParent()->getName() << "::";
        ro << "label " << val->getName();
    } else if (auto I = llvm::dyn_cast<llvm::Instruction>(val)) {
        const auto B = I->getParent();
        if (B) {
            ro << B->getParent()->getName() << "::";
        } else {
            ro << "<null>::";
        }
        ro << *val;
    } else {
        ro << *val;
    }

    ro.flush();

    // break the string if it is too long
    std::string str = ostr.str();
    if (str.length() > 50) {
        str.resize(40);
    }

    // escape the "
    size_t pos = 0;
    while ((pos = str.find('"', pos)) != std::string::npos) {
        str.replace(pos, 1, "\\\"");
        // we replaced one char with two, so we must shift after the new "
        pos += 2;
    }

    os << str;

    return os;
}

static std::ostream& operator<<(std::ostream& os, const sdg::DGNode& nd) {
    os << "N" << nd.getDG().getID() << "_" << nd.getID();
    return os;
}


class SDG2Dot {
    SystemDependenceGraph* _llvmsdg;

    // keep track of dumped nodes for checking that we dumped all
    mutable std::set<sdg::DGNode *> dumpedNodes;

    void dumpNode(std::ostream& out, sdg::DGNode& nd,
                  const llvm::Value *v = nullptr) const {
        assert(sdg::DGNode::get(&nd) && "Wrong type");

        auto& dg = nd.getDG();
        out << "      " << nd <<
               " [label=\"[" << dg.getID() << "." << nd.getID() << "] ";
        if (v) {
            // this node is associated to this value
            printLLVMVal(out, v);
        } else {
            printLLVMVal(out, _llvmsdg->getValue(&nd));
        }
        out << "\"]\n";
    }

    void dumpParams(std::ostream& out,
                    sdg::DGParameters& params,
                    const std::string& name) const {
        /// input parameters
        out << "    subgraph cluster_params_in_" << &params << " {\n";
        out << "      label=\"" << name << " (input)\"\n";
        for (auto& param : params) {
            auto& nd = param.getInputArgument();
            dumpedNodes.insert(&nd);
            dumpNode(out, nd, _llvmsdg->getValue(&param));
        }
        out << "    }\n";

        /// output parameters 
        out << "    subgraph cluster_params_out_" << &params << " {\n";
        out << "      label=\"" << name << " (output)\"\n";
        for (auto& param : params) {
            auto& nd = param.getOutputArgument();
            dumpedNodes.insert(&nd);
            dumpNode(out, nd, _llvmsdg->getValue(&param));
        }
        out << "    }\n";
    }

public:
    SDG2Dot(SystemDependenceGraph *sdg) : _llvmsdg(sdg) {}

    void dump(const std::string& file) const {
        std::ofstream out(file);
        std::set<sdg::DGNodeCall *> calls;

        out << "digraph SDG {\n";
        out << "  compound=\"true\"\n";

        for (auto *dg : _llvmsdg->getSDG()) {
            ///
            // Dependence graphs (functions) 
            out << "  subgraph cluster_dg_" << dg->getID() << " {\n";
            out << "    color=black;\n";
            out << "    style=filled;\n";
            out << "    fillcolor=grey95;\n";
            out << "    label=\"" << dg->getName() << " (id " << dg->getID() << ")\";\n";
            out << "\n";

            ///
            // Parameters of the DG
            //
            /// Formal input parameters 
            dumpParams(out, dg->getParameters(), "formal parameters");

            ///
            // Basic blocks
            for (auto *blk : dg->getBBlocks()) {
                out << "    subgraph cluster_dg_" << dg->getID() << "_bb_"
                                                  << blk->getID() << " {\n";
                out << "      label=\"bblock #" << blk->getID() << "\"\n";
                for (auto *nd : blk->getNodes()) {
                    dumpedNodes.insert(nd);
                    dumpNode(out, *nd);

                    if (auto *C = sdg::DGNodeCall::get(nd)) {
                        // store the node for later use (dumping of call edges etc.)
                        calls.insert(C);
                        // dump actual parameters
                        dumpParams(out, C->getParameters(), "actual parameters");
                    }
                }
                out << "    }\n";
            }

            ///
            // -- edges --
            out << "    /* edges */\n";
            for (auto *blk : dg->getBBlocks()) {
                for (auto *nd : blk->getNodes()) {
                    for (auto *use : nd->uses()) {
                        out << "    " << *nd << " -> " << *use
                            << "[style=\"dashed\"]\n";
                    }
                }
            }
 

            out << "  }\n";

            dumpedNodes.clear();
        }

        ////
        // -- Interprocedural edges --

        if (!calls.empty()) {
            out << " /* call edges */\n";
        }
        for (auto *C : calls) {
            for (auto *dg : C->getCallees()) {
                out << "  " << *C
                    << " -> " << *dg->getFirstNode()
                    << "[lhead=cluster_dg_" << dg->getID()
                    << " label=\"call '" << dg->getName()<< "'\""
                    << " style=bold]\n";
            }
        }

        out << "}\n";
    }
};

} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_SDG2DOT_H_
