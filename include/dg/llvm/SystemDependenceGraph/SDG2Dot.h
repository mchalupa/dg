#ifndef DG_LLVM_SDG2DOT_H_
#define DG_LLVM_SDG2DOT_H_

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "llvm/IR/Instructions.h"

#if ((LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR <= 4))
#include "llvm/DebugInfo.h" //DIScope
#else
#include "llvm/IR/DebugInfo.h" //DIScope
#endif

#include "dg/SystemDependenceGraph/DGNodeCall.h"
#include "dg/llvm/SystemDependenceGraph/SystemDependenceGraph.h"

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

namespace {
inline std::ostream &printLLVMVal(std::ostream &os, const llvm::Value *val) {
    if (!val) {
        os << "(null)";
        return os;
    }

    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    if (llvm::isa<llvm::Function>(val)) {
        ro << "FUN " << val->getName();
    } else if (const auto *B = llvm::dyn_cast<llvm::BasicBlock>(val)) {
        ro << B->getParent()->getName() << "::";
        ro << "label " << val->getName();
    } else if (const auto *I = llvm::dyn_cast<llvm::Instruction>(val)) {
        const auto *const B = I->getParent();
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
} // anonymous namespace

static std::ostream &operator<<(std::ostream &os, const sdg::DGElement &nd) {
    os << "elem" << nd.getDG().getID() << "_" << nd.getID();
    return os;
}

class SDG2Dot {
    SystemDependenceGraph *_llvmsdg;

    // keep track of dumped nodes for checking that we dumped all
    mutable std::set<sdg::DGNode *> dumpedNodes;

    void dumpNode(std::ostream &out, sdg::DGNode &nd,
                  const llvm::Value *v = nullptr,
                  const char *descr = nullptr) const {
        assert(sdg::DGNode::get(&nd) && "Wrong type");

        auto &dg = nd.getDG();
        out << "      " << nd << " [label=\"[" << dg.getID() << "."
            << nd.getID() << "] ";
        if (v) {
            // this node is associated to this value
            printLLVMVal(out, v);
        } else {
            printLLVMVal(out, _llvmsdg->getValue(&nd));
        }
        if (descr) {
            out << " " << descr;
        }
        out << "\"]\n";
    }

    void dumpParams(std::ostream &out, sdg::DGParameters &params,
                    const std::string &name) const {
        /// input parameters
        out << "    subgraph cluster_params_in_" << &params << " {\n";
        out << "      label=\"" << name << " (input)\"\n";
        for (auto &param : params) {
            auto &nd = param.getInputArgument();
            dumpedNodes.insert(&nd);
            dumpNode(out, nd, _llvmsdg->getValue(&param));
        }
        out << "    }\n";

        /// output parameters
        out << "    subgraph cluster_params_out_" << &params << " {\n";
        out << "      label=\"" << name << " (output)\"\n";
        for (auto &param : params) {
            auto &nd = param.getOutputArgument();
            dumpedNodes.insert(&nd);
            dumpNode(out, nd, _llvmsdg->getValue(&param));
        }
        if (auto *noret = params.getNoReturn()) {
            dumpedNodes.insert(noret);
            dumpNode(out, *noret, nullptr, "noret");
        }
        if (auto *ret = params.getReturn()) {
            dumpedNodes.insert(ret);
            dumpNode(out, *ret, nullptr, "ret");
        }
        out << "    }\n";
    }

    void dumpParamEdges(std::ostream &out, sdg::DGParameters &params) const {
        /// input parameters
        for (auto &param : params) {
            dumpEdges(out, param.getInputArgument());
        }

        /// output parameters
        for (auto &param : params) {
            dumpEdges(out, param.getOutputArgument());
        }
        if (auto *noret = params.getNoReturn()) {
            dumpEdges(out, *noret);
        }
        if (auto *ret = params.getReturn()) {
            dumpEdges(out, *ret);
        }
    }

    void bindParamsToCall(std::ostream &out, sdg::DGParameters &params,
                          sdg::DGNode *call) const {
        /// input parameters
        for (auto &param : params) {
            auto &nd = param.getOutputArgument();
            out << "      " << *call << " -> " << nd << "[style=dashed]\n";
        }

        if (auto *noret = params.getNoReturn()) {
            out << "      " << *call << " -> " << *noret << "[style=dashed]\n";
        }
        if (auto *ret = params.getReturn()) {
            out << "      " << *call << " -> " << *ret << "[style=dashed]\n";
        }
    }

    void dumpEdges(std::ostream &out, sdg::DGNode &nd) const {
        for (auto *use : nd.uses()) {
            out << "    " << nd << " -> " << *use << "[style=\"dashed\"]\n";
        }
        for (auto *def : nd.memdep()) {
            out << "    " << *def << " -> " << nd << "[color=red]\n";
        }
        for (auto *ctrl : nd.controls()) {
            out << "    " << nd << " -> " << *ctrl << "[color=blue]\n";
        }
    }

  public:
    SDG2Dot(SystemDependenceGraph *sdg) : _llvmsdg(sdg) {}

    void dump(const std::string &file) const {
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
            out << "    label=\"" << dg->getName() << " (id " << dg->getID()
                << ")\";\n";
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
                        // store the node for later use (dumping of call edges
                        // etc.)
                        calls.insert(C);
                        // dump actual parameters
                        dumpParams(out, C->getParameters(),
                                   "actual parameters");
                    }
                }
                out << "    }\n";
            }

            ///
            // -- edges --
            out << "    /* edges */\n";
            for (auto *nd : dg->getNodes()) {
                dumpEdges(out, *nd);
            }
            out << "    /* block edges */\n";
            for (auto *blk : dg->getBBlocks()) {
                for (auto *ctrl : blk->controls()) {
                    out << "    " << *blk->back() << " -> ";
                    if (auto *ctrlB = sdg::DGBBlock::get(ctrl)) {
                        out << *ctrlB->front();
                    } else {
                        out << *ctrl;
                    }

                    out << "[color=blue penwidth=2 "
                        << " ltail=cluster_dg_" << dg->getID() << "_bb_"
                        << blk->getID();

                    if (ctrl->getType() == sdg::DGElementType::BBLOCK) {
                        out << " lhead=cluster_dg_" << dg->getID() << "_bb_"
                            << ctrl->getID();
                    }
                    out << "]\n";
                }
            }

            out << "  }\n";

            // formal parameters edges
            dumpParamEdges(out, dg->getParameters());

            dumpedNodes.clear();
        }

        ////
        // -- Interprocedural edges and parameter edges--

        if (!calls.empty()) {
            out << " /* call and param edges */\n";
        }
        for (auto *C : calls) {
            bindParamsToCall(out, C->getParameters(), C);
            dumpParamEdges(out, C->getParameters());
            for (auto *dg : C->getCallees()) {
                out << "  " << *C << " -> " << *dg->getFirstNode()
                    << "[lhead=cluster_dg_" << dg->getID() << " label=\"call '"
                    << dg->getName() << "'\""
                    << " style=dashed penwidth=3]\n";
            }
        }

        out << "}\n";
    }
};

} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_SDG2DOT_H_
