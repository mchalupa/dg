#include <cassert>
#include <iostream>
#include <ctime>
#include <random>

#include <dg/util/SilenceLLVMWarnings.h>
SILENCE_LLVM_WARNINGS_PUSH
#include <llvm/Support/CommandLine.h>
SILENCE_LLVM_WARNINGS_POP

#include "dg/tools/llvm-slicer-opts.h"

#include "dg/util/debug.h"
#include "dg/ADT/Queue.h"

#include "ControlDependence/CDGraph.h"
#include "ControlDependence/NTSCD.h"
#include "ControlDependence/DOD.h"
#include "ControlDependence/DODNTSCD.h"

using namespace dg;

using llvm::errs;

llvm::cl::opt<bool> enable_debug("dbg",
    llvm::cl::desc("Enable debugging messages (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> quiet("q",
    llvm::cl::desc("Do not generate output, just run the analysis "
                   "(e.g., for performance analysis) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> total_only("total-only",
    llvm::cl::desc("Do not generate output other than the total time (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> fun_info_only("fun-info-only",
    llvm::cl::desc("Only dump statistics about the functions in module (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> scd("scd",
    llvm::cl::desc("Benchmark standard CD (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> ntscd("ntscd",
    llvm::cl::desc("Benchmark NTSCD (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> ntscd2("ntscd2",
    llvm::cl::desc("Benchmark NTSCD 2 (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> ntscd_ranganath("ntscd-ranganath",
    llvm::cl::desc("Benchmark NTSCD (Ranganath algorithm) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> ntscd_legacy("ntscd-legacy",
    llvm::cl::desc("Benchmark NTSCD (legacy implementation) (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dod("dod",
    llvm::cl::desc("Benchmark DOD (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dod_ranganath("dod-ranganath",
    llvm::cl::desc("Benchmark DOD (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> dod_ntscd("dod+ntscd",
    llvm::cl::desc("Benchmark DOD + NTSCD (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> scc("scc",
    llvm::cl::desc("Strong control closure (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<bool> compare("compare",
    llvm::cl::desc("Compare the resulting control dependencies (default=false)."),
    llvm::cl::init(false), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<unsigned> Vn("nodes",
    llvm::cl::desc("The number of nodes (default=100)."),
    llvm::cl::init(100), llvm::cl::cat(SlicingOpts));

llvm::cl::opt<unsigned> En("edges",
    llvm::cl::desc("The number of edges (default=1.5*nodes)."),
    llvm::cl::init(0), llvm::cl::cat(SlicingOpts));

void generateRandomGraph(CDGraph& G, unsigned Vnum = 100, unsigned Enum = 0) {
    if (Enum == 0 || Enum > 2*Vnum)
        Enum = Vnum;

    std::vector<CDNode*> nodes;
    nodes.push_back(nullptr);
    nodes.reserve(Vnum + 1);
    for (unsigned i = 0; i < Vnum; ++i) {
        auto& nd = G.createNode();
        nodes.push_back(&nd);
        assert(nodes.size() - 1 == nd.getID());
    }
    // create random edges
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> ids(1,Vnum);

    unsigned n = 0;
    while (Enum > 0 && ++n < 10*Enum) {
        auto id1 = ids(rng);
        if (nodes[id1]->successors().size() > 1)
            continue;
        auto id2 = ids(rng);
        G.addNodeSuccessor(*nodes[id1], *nodes[id2]);
        //std::cout << id1 << " -> " << id2 << "\n";
        --Enum;
    }
}

int main(int argc, char *argv[])
{
    SlicerOptions options = parseSlicerOptions(argc, argv,
                                               /* requireCrit = */ false,
                                               /* inputFileRequired = */ false);
    if (enable_debug) {
        DBG_ENABLE();
    }

    CDGraph G;
    if (En == 0)
        En = (unsigned)1.5*Vn;
    generateRandomGraph(G, Vn, En);

    clock_t start, end, elapsed;

    if (ntscd) {
        dg::NTSCD ntscd;
        start = clock();
        ntscd.compute(G);
        end = clock();
        elapsed = end - start;

        std::cout << "ntscd: "
                  << static_cast<float>(elapsed) / CLOCKS_PER_SEC << " s ("
                  << elapsed << " ticks)\n";
    }
    if (ntscd2) {
        dg::NTSCD2 ntscd;
        start = clock();
        ntscd.compute(G);
        end = clock();
        elapsed = end - start;

        std::cout << "ntscd: "
                  << static_cast<float>(elapsed) / CLOCKS_PER_SEC << " s ("
                  << elapsed << " ticks)\n";
    }
    if (ntscd_ranganath) {
        dg::NTSCDRanganath ntscd;
        start = clock();
        ntscd.compute(G);
        end = clock();
        elapsed = end - start;

        std::cout << "ntscd: "
                  << static_cast<float>(elapsed) / CLOCKS_PER_SEC << " s ("
                  << elapsed << " ticks)\n";
    }
    if (dod) {
        dg::DOD dod;
        start = clock();
        dod.compute(G);
        end = clock();
        elapsed = end - start;

        std::cout << "dod: "
                  << static_cast<float>(elapsed) / CLOCKS_PER_SEC << " s ("
                  << elapsed << " ticks)\n";
    }
    if (dod_ranganath) {
        dg::DODRanganath ntscd;
        start = clock();
        ntscd.compute(G);
        end = clock();
        elapsed = end - start;

        std::cout << "dod-ranganath: "
                  << static_cast<float>(elapsed) / CLOCKS_PER_SEC << " s ("
                  << elapsed << " ticks)\n";
    }
    if (dod_ntscd) {
        dg::DODNTSCD ntscd;
        start = clock();
        ntscd.compute(G);
        end = clock();
        elapsed = end - start;

        std::cout << "dod+ntscd: "
                  << static_cast<float>(elapsed) / CLOCKS_PER_SEC << " s ("
                  << elapsed << " ticks)\n";
    }
    /*

    }
    if (ntscd_legacy) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::NTSCD_LEGACY;
        analyses.emplace_back("ntscd-legacy", new LLVMControlDependenceAnalysis(M.get(), opts), 0);
    }
   if (scc) {
        opts.algorithm = dg::ControlDependenceAnalysisOptions::CDAlgorithm::STRONG_CC;
        analyses.emplace_back("scc", new LLVMControlDependenceAnalysis(M.get(), opts), 0);
    }
    */
    return 0;
}
