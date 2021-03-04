#include <set>
#include <vector>
#include <string>
#include <cassert>
#include <iostream>
#include <sstream>
#include <fstream>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/Config/llvm-config.h>

#if (LLVM_VERSION_MAJOR < 3)
#error "Unsupported version of LLVM"
#endif

#include "dg/tools/llvm-slicer.h"
#include "dg/tools/llvm-slicer-opts.h"
#include "dg/tools/llvm-slicer-utils.h"

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR <= 7
#include <llvm/IR/LLVMContext.h>
#endif
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/InstIterator.h>
SILENCE_LLVM_WARNINGS_POP

#ifdef HAVE_SVF
#include "dg/llvm/PointerAnalysis/SVFPointerAnalysis.h"
#endif
#include "dg/llvm/PointerAnalysis/DGPointerAnalysis.h"
#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/ControlDependence/ControlDependence.h"
#include "dg/util/debug.h"

#include "ControlDependence/CDGraph.h"
#include "llvm/ControlDependence/NTSCD.h"
#include "llvm/ControlDependence/DOD.h"

using namespace dg;

using llvm::errs;

llvm::cl::opt<bool> enable_debug("dbg",
    llvm::cl::desc("Enable debugging messages (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> show_cfg("cfg",
    llvm::cl::desc("Show CFG edges (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dump_ir("ir",
    llvm::cl::desc("Show internal representation instead of LLVM (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> stats("statistics",
    llvm::cl::desc("Dump statistics(default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> quiet("q",
    llvm::cl::desc("Do not generate output, just run the analysis "
                   "(e.g., for performance analysis) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> todot("dot",
    llvm::cl::desc("Output in graphviz format (forced atm.)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dump_c_lines("c-lines",
    llvm::cl::desc("Dump output as C lines (line:column where possible)."
                   "Requires metadata in the bitcode (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> use_pta("use-pta",
    llvm::cl::desc("Use pointer analysis to build call graph. "
                   "Makes sense only with -cda-icfg switch (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

using VariablesMapTy = std::map<const llvm::Value *, CVariableDecl>;
VariablesMapTy allocasToVars(const llvm::Module& M);
VariablesMapTy valuesToVars;

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
        SMD.print("llvm-cda-dump", llvm::errs());
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

static std::string
getInstName(const llvm::Value *val) {
    assert(val);
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    if (dump_c_lines) {
        if (auto *I = llvm::dyn_cast<llvm::Instruction>(val)) {
            auto& DL = I->getDebugLoc();
            if (DL) {
                ro << DL.getLine() << ":" << DL.getCol();
            } else {
                auto Vit = valuesToVars.find(I);
                if (Vit != valuesToVars.end()) {
                    auto& decl = Vit->second;
                    ro << decl.line << ":" << decl.col;
                } else {
                    ro << "(no dbg) ";
                    ro << *val;
                }
            }
        } else {
            ro << "(no inst) ";
            ro << *val;
        }
        ro.flush();
        return ostr.str();
    }

    if (llvm::isa<llvm::Function>(val))
        ro << val->getName().data();
    else
        ro << *val;

    ro.flush();

    // break the string if it is too long
    return ostr.str();
}

static inline void dumpEdge(const llvm::Value *from,
                            const llvm::Value *to,
                            const char *attrs = nullptr) {
    using namespace llvm;

    auto *fromB = dyn_cast<BasicBlock>(from);
    auto *toB = dyn_cast<BasicBlock>(to);

    std::cout << "instr";
    if (fromB) {
        std::cout << &fromB->back();
    } else {
        std::cout << from;
    }

    std::cout << " -> instr";
    if (toB) {
        std::cout << &toB->front();
    } else {
        std::cout << to;
    }

    std::cout << "[";
    if (attrs) {
        std::cout << attrs;
    } else {
        std::cout << "color=blue minlen=2 penwidth=2";
    }
    if (fromB || toB) {
        if (fromB) {
            std::cout << " ltail=cluster_bb_" << fromB;
        }
        if (toB) {
            std::cout << " lhead=cluster_bb_" << toB;
        }
    }
    std::cout << "]";

    std::cout << "\n";
}

static void dumpCdaToDot(LLVMControlDependenceAnalysis& cda, const llvm::Module *m) {
    std::cout << "digraph ControlDependencies {\n";
    std::cout << "  compound=true;\n";

    // dump nodes
    for (auto& f : *m) {
        if (f.isDeclaration())
            continue;

        std::cout << "subgraph cluster_f_" << f.getName().str() << " {\n";
        std::cout << "label=\"" << f.getName().str() << "\"\n";
        for (auto& b : f) {
            std::cout << "subgraph cluster_bb_" << &b << " {\n";
            std::cout << "  style=dotted;\n";
            for (auto& I : b) {
              std::cout << " instr" << &I << " [shape=rectangle label=\"" << getInstName(&I) << "\"]\n";
            }

            const llvm::Instruction *last = nullptr;
            // give the block top-down structure
            for (auto& I : b) {
                if (last) {
                    std::cout << " instr" << last << " -> " << "instr" << &I;
                    if (show_cfg) {
                        std::cout << " [style=dotted]\n";
                    } else {
                        std::cout << " [style=invis]\n";
                    }
               }
               last = &I;
            }
            std::cout << "}\n";
        }
        std::cout << "}\n";
    }


    // dump CFG edges between blocks
    if (show_cfg) {
        for (auto& f : *m) {
            for (auto& b : f) {
                for (auto *succ : successors(&b)) {
                    dumpEdge(&b, succ, "style=dashed minlen=2 color=black");
                }
            }
        }
    }

    // dump edges
    for (auto& f : *m) {
        for (auto& b : f) {
            for (auto *D : cda.getDependencies(&b)) {
                dumpEdge(D, &b);
            }

            for (auto& I : b) {
              for (auto *D : cda.getDependencies(&I)) {
                  dumpEdge(D, &I);
              }
            }
        }
    }

    std::cout << "}\n";
}

static void dumpCda(LLVMControlDependenceAnalysis& cda) {
    const auto *m = cda.getModule();

    if (dump_c_lines) {
#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7)
        llvm::errs() << "WARNING: Variables names matching is not supported for LLVM older than 3.7\n";
#else
        valuesToVars = allocasToVars(*m);
#endif // LLVM > 3.6
        if (valuesToVars.empty()) {
            llvm::errs() << "WARNING: No debugging information found, "
                         << "the C lines output will be corrupted\n";
        }
    }

    if (todot) {
        dumpCdaToDot(cda, m);
        return;
    }

    for (auto& F : *m) {
        for (auto& B : F) {
            for (auto& I : B) {
                for (auto *dep : cda.getDependencies(&B)) {
                    auto *depB = llvm::cast<llvm::BasicBlock>(dep);
                    std::cout << getInstName(&I) << " -> "
                              << getInstName(depB->getTerminator()) << "\n";
                }

                for (auto *dep : cda.getDependencies(&I)) {
                    std::cout << getInstName(&I) << " -> "
                              << getInstName(dep) << "\n";
                }
            }
        }
    }
}

static void dump_graph(CDGraph *graph) {
    assert(graph);
    // dump nodes
    for (const auto *nd : *graph) {
        std::cout << " " << graph->getName() << "_" << nd->getID()
                  << " [label=\"" << graph->getName() << ":" << nd->getID() << "\"";
        if (graph->isPredicate(*nd)) {
            std::cout << " color=blue";
        }
        std::cout << "]\n";
    }

    // dump edges
    for (const auto *nd : *graph) {
        for (const auto *succ : nd->successors()) {
            std::cout << " " << graph->getName() << "_" << nd->getID()
                      << " -> " << graph->getName() << "_" << succ->getID() << "\n";
        }
    }
}

static void dumpIr(LLVMControlDependenceAnalysis& cda) {
    const auto *m = cda.getModule();
    auto *impl = cda.getImpl();

    std::cout << "digraph ControlDependencies {\n";
    std::cout << "  compound=true;\n";

    if (cda.getOptions().ICFG()) {
        cda.compute();
        dump_graph(impl->getGraph(nullptr));
        std::cout << "}\n";
        return;
    }

    // dump nodes
    for (const auto& f : *m) {
        cda.compute(&f);
        auto *graph = impl->getGraph(&f);
        if (!graph)
            continue;
        std::cout << "subgraph cluster_f_" << f.getName().str() << " {\n";
        std::cout << "label=\"" << f.getName().str() << "\"\n";

        dump_graph(graph);

        if (cda.getOptions().ntscdCD() || cda.getOptions().ntscd2CD() ||
            cda.getOptions().ntscdRanganathCD()) {
            auto *ntscd = static_cast<dg::llvmdg::NTSCD*>(impl);
            const auto *info = ntscd->_getFunInfo(&f);
            if (info) {
                for (auto *nd : *graph) {
                    auto it = info->controlDependence.find(nd);
                    if (it == info->controlDependence.end())
                        continue;

                    for (const auto *dep : it->second) {
                        // FIXME: for interproc CD this will not work as the nodes
                        // would be in a different graph
                        std::cout << " " << graph->getName() << "_" << dep->getID()
                                  << " -> " << graph->getName() << "_" << nd->getID()
                                  << " [ color=red ]\n";
                    }
                }
            }
        } else if (cda.getOptions().dodCD() ||
                   cda.getOptions().dodRanganathCD() ||
                   cda.getOptions().dodntscdCD()) {
            auto *dod = static_cast<dg::llvmdg::DOD*>(impl);
            const auto *info = dod->_getFunInfo(&f);
            if (info) {
                for (auto *nd : *graph) {
                    auto it = info->controlDependence.find(nd);
                    if (it == info->controlDependence.end())
                        continue;

                    for (const auto *dep : it->second) {
                        std::cout << " " << graph->getName() << "_" << dep->getID()
                                  << " -> " << graph->getName() << "_" << nd->getID()
                                  << " [ color=red ]\n";
                    }
                }
            }
        }

        std::cout << "}\n";
    }

    std::cout << "}\n";
}

int main(int argc, char *argv[])
{
    setupStackTraceOnError(argc, argv);

    SlicerOptions options = parseSlicerOptions(argc, argv,
                                               /* requireCrit = */ false);

    if (enable_debug) {
        DBG_ENABLE();
    }

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

    LLVMControlDependenceAnalysis cda(M.get(), options.dgOptions.CDAOptions);

    if (quiet) {
        cda.compute(); // compute all the information
        if (stats) {
            // FIXME
            //dumpStats(cda);
        }
    } else {
        if (dump_ir) {
            dumpIr(cda);
        } else {
            dumpCda(cda);
        }
    }

    return 0;
}
