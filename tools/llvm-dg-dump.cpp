#include <assert.h>
#include <cstdio>

#include <set>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Bitcode/ReaderWriter.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include "llvm/LLVMDependenceGraph.h"
#include "llvm/PointsTo.h"
#include "llvm/ReachingDefs.h"
#include "llvm/DefUse.h"
#include "llvm/Slicer.h"
#include "DG2Dot.h"
#include "Utils.h"

#include "analysis/PointsToFlowSensitive.h"
#include "analysis/PointsToFlowInsensitive.h"
#include "llvm/LLVMPointsToAnalysis.h"
#include "llvm/LLVMReachingDefinitions.h"

using namespace dg;
using llvm::errs;

static bool debug_def = false;
static bool debug_ptr = false;

static std::ostream& operator<<(std::ostream& os, const analysis::Offset& off)
{
    if (off.offset == UNKNOWN_OFFSET)
        os << "UNKNOWN";
    else
        os << off.offset;

    return os;
}

static std::ostream& printLLVMVal(std::ostream& os, const llvm::Value *val)
{
    if (!val) {
        os << "(null)";
        return os;
    }

    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    if (llvm::isa<llvm::Function>(val)) {
        ro << "FUNC " << val->getName().data();
    } else if (llvm::isa<llvm::BasicBlock>(val)) {
        ro << "label " << val->getName().data();
    } else {
        ro << *val;
    }

    ro.flush();

    // break the string if it is too long
    std::string str = ostr.str();
    if (str.length() > 100) {
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

class LLVMDG2Dot : public debug::DG2Dot<LLVMNode>
{
public:

    LLVMDG2Dot(LLVMDependenceGraph *dg,
                  uint32_t opts = debug::PRINT_CFG | debug::PRINT_DD | debug::PRINT_CD,
                  const char *file = NULL)
        : debug::DG2Dot<LLVMNode>(dg, opts, file) {}

    /* virtual */
    std::ostream& printKey(std::ostream& os, const llvm::Value *val)
    {
        return printLLVMVal(os, val);
    }

    /* virtual */
    bool checkNode(std::ostream& os, LLVMNode *node)
    {
        bool err = false;
        const llvm::Value *val = node->getKey();

        if (!val) {
            os << "\\nERR: no value in node";
            return true;
        }

        if (!node->getBBlock()
            && !llvm::isa<llvm::Function>(val)
            && !llvm::isa<llvm::GlobalVariable>(val)) {
            err = true;
            os << "\\nERR: no BB";
        }

        if (debug_ptr) {
            const analysis::PointsToSetT& ptsto = node->getPointsTo();
            if (ptsto.empty() && val->getType()->isPointerTy()) {
                os << "\\lERR: pointer without pointsto set";
                err = true;
            } else
                os << "\\l -- points-to info --";

            for (auto it : ptsto) {
                os << "\\l[";
                if (it.isUnknown())
                    os << "unknown";
                else if (it.obj->isUnknown())
                    os << "unknown mem";
                else if (it.obj->isNull())
                    os << "nullptr";
                else
                    printLLVMVal(os, it.obj->node->getKey());
                os << "] + " << it.offset;
            }

            analysis::MemoryObj *mo = node->getMemoryObj();
            if (mo) {
                for (auto it : mo->pointsTo) {
                    for(auto it2 : it.second) {
                        os << "\\lmem: [" << it.first << "] -> [";
                        if (it2.isUnknown())
                            os << "unknown";
                        else if (it2.obj->isUnknown())
                            os << "unknown mem";
                        else if (it2.obj->isNull())
                            os << "nullptr";
                        else
                            printLLVMVal(os, it2.obj->node->getKey());
                        os << "] + " << it2.offset;
                    }
                }
            }
        }

        if (debug_def) {
            analysis::DefMap *df = node->getData<analysis::DefMap>();
            if (df) {
                os << "\\l -- reaching defs info --";
                for (auto it : df->getDefs()) {
                    for (auto v : it.second) {
                        os << "\\l: [";
                        if (it.first.obj->isUnknown())
                            os << "[unknown";
                        else
                            printLLVMVal(os, it.first.obj->node->getKey());
                        os << "] + " << it.first.offset << " @ ";
                        printLLVMVal(os, v->getKey());
                    }
                }
            }
        }

        return err;
    }

    bool dump(const char *new_file = nullptr, const char *dump_func_only = nullptr)
    {
        // make sure we have the file opened
        if (!ensureFile(new_file))
            return false;

        const std::map<const llvm::Value *,
                       LLVMDependenceGraph *>& CF = getConstructedFunctions();

        start();

        for (auto F : CF) {
            if (dump_func_only && !F.first->getName().equals(dump_func_only))
                continue;

            dumpSubgraph(F.second, F.first->getName().data());
        }

        end();

        return true;
    }

private:

    void dumpSubgraph(LLVMDependenceGraph *graph, const char *name)
    {
        dumpSubgraphStart(graph, name);

        for (auto B : graph->getBlocks()) {
            dumpBBlock(B.second);
        }

        for (auto B : graph->getBlocks()) {
            dumpBBlockEdges(B.second);
        }

        dumpSubgraphEnd(graph);
    }
};

class LLVMDGDumpBlocks : public debug::DG2Dot<LLVMNode>
{
public:

    LLVMDGDumpBlocks(LLVMDependenceGraph *dg,
                  uint32_t opts = debug::PRINT_CFG | debug::PRINT_DD | debug::PRINT_CD,
                  const char *file = NULL)
        : debug::DG2Dot<LLVMNode>(dg, opts, file) {}

    /* virtual
    std::ostream& printKey(std::ostream& os, const llvm::Value *val)
    {
        return printLLVMVal(os, val);
    }
    */

    /* virtual */
    bool checkNode(std::ostream& os, LLVMNode *node)
    {
        return false; // no error
    }

    bool dump(const char *new_file = nullptr , const char *dump_func_only = nullptr)
    {
        // make sure we have the file opened
        if (!ensureFile(new_file))
            return false;

        const std::map<const llvm::Value *,
                       LLVMDependenceGraph *>& CF = getConstructedFunctions();

        start();

        for (auto F : CF) {
            if (dump_func_only && !F.first->getName().equals(dump_func_only))
                continue;

            dumpSubgraph(F.second, F.first->getName().data());
        }

        end();

        return true;
    }

private:

    void dumpSubgraph(LLVMDependenceGraph *graph, const char *name)
    {
        dumpSubgraphStart(graph, name);

        for (auto B : graph->getBlocks()) {
            dumpBlock(B.second);
        }

        for (auto B : graph->getBlocks()) {
            dumpBlockEdges(B.second);
        }

        dumpSubgraphEnd(graph, false);
    }

    void dumpBlock(LLVMBBlock *blk)
    {
        out << "NODE" << blk << " [label=\"";

        std::ostringstream ostr;
        llvm::raw_os_ostream ro(ostr);

        ro << *blk->getKey();
        ro.flush();
        std::string str = ostr.str();

        unsigned int i = 0;
        unsigned int len = 0;
        while (str[i] != 0) {
            if (len >= 40) {
                str[i] = '\n';
                len = 0;
            } else
                ++len;

            if (str[i] == '\n')
                len = 0;

            ++i;
        }

        unsigned int slice_id = blk->getSlice();
        if (slice_id != 0)
            out << "\\nslice: "<< slice_id << "\\n";
        out << str << "\"";

        if (slice_id != 0)
            out << "style=filled fillcolor=greenyellow";

        out << "]\n";
    }

    void dumpBlockEdges(LLVMBBlock *blk)
    {
        for (const LLVMBBlock::BBlockEdge& edge : blk->successors()) {
            out << "NODE" << blk << " -> NODE" << edge.target
                << " [penwidth=2 label=\""<< (int) edge.label << "\"] \n";
        }

        for (const LLVMBBlock *pdf : blk->getPostDomFrontiers()) {
            out << "NODE" << blk << " -> NODE" << pdf
                << "[color=purple constraint=false]\n";
        }
    }
};

int main(int argc, char *argv[])
{
    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;
    llvm::Module *M;
    bool mark_only = false;
    bool bb_only = false;
    const char *module = nullptr;
    const char *slicing_criterion = nullptr;
    const char *dump_func_only = nullptr;
    const char *pts = nullptr;

    using namespace debug;
    uint32_t opts = PRINT_CFG | PRINT_DD | PRINT_CD;

    // parse options
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-no-control") == 0) {
            opts &= ~PRINT_CD;
        } else if (strcmp(argv[i], "-pta") == 0) {
            pts = argv[++i];
        } else if (strcmp(argv[i], "-no-data") == 0) {
            opts &= ~PRINT_DD;
        } else if (strcmp(argv[i], "-nocfg") == 0) {
            opts &= ~PRINT_CFG;
        } else if (strcmp(argv[i], "-call") == 0) {
            opts |= PRINT_CALL;
        } else if (strcmp(argv[i], "-postdom") == 0) {
            opts |= PRINT_POSTDOM;
        } else if (strcmp(argv[i], "-bb-only") == 0) {
            bb_only = true;
        } else if (strcmp(argv[i], "-cfgall") == 0) {
            opts |= PRINT_CFG;
            opts |= PRINT_REV_CFG;
        } else if (strcmp(argv[i], "-func") == 0) {
            dump_func_only = argv[++i];
        } else if (strcmp(argv[i], "-def") == 0) {
            debug_def = true;
        } else if (strcmp(argv[i], "-ptr") == 0) {
            debug_ptr = true;
        } else if (strcmp(argv[i], "-slice") == 0) {
            slicing_criterion = argv[++i];
        } else if (strcmp(argv[i], "-mark") == 0) {
            mark_only = true;
            slicing_criterion = argv[++i];
        } else {
            module = argv[i];
        }
    }

    if (!module) {
        errs() << "Usage: % IR_module [output_file]\n";
        return 1;
    }

    M = llvm::ParseIRFile(module, SMD, context);
    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    debug::TimeMeasure tm;

    LLVMDependenceGraph d;
    // TODO refactor the code...

    LLVMPointsToAnalysis *PTA = nullptr;
    if (pts) {
        // use new analyses
        if (strcmp(pts, "fs") == 0) {
            PTA = new LLVMPointsToAnalysisImpl<analysis::pss::PointsToFlowSensitive>(M);
        } else if (strcmp(pts, "fi") == 0) {
            PTA = new LLVMPointsToAnalysisImpl<analysis::pss::PointsToFlowInsensitive>(M);
        } else {
            llvm::errs() << "Unknown points to analysis, try: fs, fi\n";
            abort();
        }

        tm.start();
        PTA->run();
        tm.stop();
        tm.report("INFO: Points-to analysis took");

        d.build(M, PTA);
    } else {
        d.build(M);
        analysis::LLVMPointsToAnalysis PTA(&d);

        tm.start();
        PTA.run();
        tm.stop();
        tm.report("INFO: Points-to analysis [old] took");
    }

    std::set<LLVMNode *> callsites;
    if (slicing_criterion) {
        const char *sc[] = {
            slicing_criterion,
            "klee_assume",
            NULL
        };

        tm.start();
        d.getCallSites(sc, &callsites);
        tm.stop();
        tm.report("INFO: Finding slicing criterions took");
    }

    if (pts) {
        assert(PTA && "BUG: Need points-to analysis");
        //use new analyses
        analysis::rd::LLVMReachingDefinitions RDA(M, PTA);
        tm.start();
        RDA.run();  // compute reaching definitions
        tm.stop();
        tm.report("INFO: Reaching defs analysis took");

        LLVMDefUseAnalysis DUA(&d, &RDA, PTA);
        tm.start();
        DUA.run(); // add def-use edges according that
        tm.stop();
        tm.report("INFO: Adding Def-Use edges took");
    } else {
        analysis::LLVMReachingDefsAnalysis RDA(&d);
        tm.start();
        RDA.run();  // compute reaching definitions
        tm.stop();
        tm.report("INFO: Reaching defs analysis [old] took");

        analysis::old::LLVMDefUseAnalysis DUA(&d);
        tm.start();
        DUA.run(); // add def-use edges according that
        tm.stop();
        tm.report("INFO: Adding Def-Use edges [old] took");
    }

    tm.start();
    // add post-dominator frontiers
    d.computePostDominators(true);
    tm.stop();
    tm.report("INFO: computing post-dominator frontiers took");

    if (slicing_criterion) {
        LLVMSlicer slicer;
        tm.start();

        if (strcmp(slicing_criterion, "ret") == 0) {
            if (mark_only)
                slicer.mark(d.getExit());
            else
                slicer.slice(&d, d.getExit());
        } else {
            if (callsites.empty()) {
                errs() << "ERR: slicing criterion not found: "
                       << slicing_criterion << "\n";
                exit(1);
            }

            uint32_t slid = 0;
            for (LLVMNode *start : callsites)
                slid = slicer.mark(start, slid);

            if (!mark_only)
               slicer.slice(&d, nullptr, slid);
        }

        // there's overhead but nevermind
        tm.stop();
        tm.report("INFO: Slicing took");

        if (!mark_only) {
            std::string fl(module);
            fl.append(".sliced");
            std::ofstream ofs(fl);
            llvm::raw_os_ostream output(ofs);

            analysis::SlicerStatistics& st = slicer.getStatistics();
            errs() << "INFO: Sliced away " << st.nodesRemoved
                   << " from " << st.nodesTotal << " nodes\n";

            llvm::WriteBitcodeToFile(M, output);
        }
    }

    if (bb_only) {
        LLVMDGDumpBlocks dumper(&d, opts);
        dumper.dump(nullptr, dump_func_only);
    } else {
        LLVMDG2Dot dumper(&d, opts);
        dumper.dump(nullptr, dump_func_only);
    }

    return 0;
}
