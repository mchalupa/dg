#include <cassert>
#include <iostream>
#include <ctime>
#include <random>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/Support/CommandLine.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

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

llvm::cl::opt<unsigned> irrcores("irreducible-cores",
    llvm::cl::desc("Generate graph that has at least N irreducible cores.\n"
                   "The resulting graph is going to be irreducible if N > 0\n"
                   "with hight probability (default=0)."),
    llvm::cl::init(0), llvm::cl::cat(SlicingOpts));

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
    // restrict only to graphs that have at most 2 successors
    if (Enum == 0)
        Enum = Vnum;
    if (Enum > 2*Vnum)
        Enum = 2*Vnum;

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

    while (Enum > 0) {
        auto id1 = ids(rng);
        while (nodes[id1]->successors().size() > 1) {
            ++id1;
            if (id1 > Vnum)
                id1 = 1;
            continue;
        }
        auto id2 = ids(rng);
        G.addNodeSuccessor(*nodes[id1], *nodes[id2]);
        //std::cout << id1 << " -> " << id2 << "\n";
        --Enum;
    }

}

void generateRandomIrreducibleGraph(CDGraph& G, unsigned irredcores = 1, unsigned Vnum = 100, unsigned Enum = 0) {
    // restrict only to graphs that have at most 2 successors
    if (Vnum < 3*irredcores)
        Vnum = 3*irredcores;
    if (Enum > 2*Vnum)
        Enum = 2*Vnum;
    if (Enum < 4*irredcores)
        Enum = 4*irredcores;

    std::vector<CDNode*> nodes;
    std::vector<std::pair<CDNode*,CDNode *>> edges;
    nodes.push_back(nullptr);
    nodes.reserve(Vnum + 1);

    // create irreducible cores
    for (unsigned i = 0; i < irredcores; ++i) {
        auto& nd1 = G.createNode();
        auto& nd2 = G.createNode();
        auto& nd3 = G.createNode();
        nodes.push_back(&nd1);
        nodes.push_back(&nd2);
        nodes.push_back(&nd3);
        G.addNodeSuccessor(nd1, nd2);
        G.addNodeSuccessor(nd1, nd3);
        G.addNodeSuccessor(nd3, nd2);
        G.addNodeSuccessor(nd2, nd3);
        edges.emplace_back(&nd1, &nd2);
        edges.emplace_back(&nd1, &nd3);
        edges.emplace_back(&nd2, &nd3);
        edges.emplace_back(&nd3, &nd2);
        assert(Vnum >= 3);
        assert(Enum >= 4);
        Vnum -= 3;
        Enum -=4;
    }
    // random generator for operation
    std::random_device dev;
    std::mt19937 rngop(dev());
    // 1 - split edge
    // 2 - add node and edge
    // 3 - add edge between existing nodes
    std::uniform_int_distribution<std::mt19937::result_type> randop(1,3);

    // random device for ids
    std::random_device dev2;
    std::mt19937 rngid(dev2());
    std::uniform_int_distribution<std::mt19937::result_type> ids(1,Vnum);
    // random device for edges
    std::random_device dev3;
    std::mt19937 rnge(dev3());
    std::uniform_int_distribution<std::mt19937::result_type> eids(1,Enum);

    while (Enum > 0 || Vnum > 0) {
        auto op = randop(rngop);
        std::cout << op << " " << Enum << " " << Vnum << std::endl;
        if (op == 1 && Vnum > 0 && Enum > 0) { // split edge
            auto eid = eids(rnge) % edges.size();
            auto& nd = G.createNode();
            nodes.push_back(&nd);
            G.removeNodeSuccessor(*edges[eid].first, *edges[eid].second);
            G.addNodeSuccessor(*edges[eid].first, nd);
            G.addNodeSuccessor(nd, *edges[eid].second);
            edges[eid] = {edges[eid].first, &nd};
            edges.emplace_back(&nd, edges[eid].second);
            assert(Enum != 0);
            --Enum;
            --Vnum;
        } else if (op == 2 && Vnum > 0 && Enum > 0) { // and new node
            auto& nd = G.createNode();
            nodes.push_back(&nd);
            --Vnum;

            auto nodesnum = nodes.size() - 1;
            auto id = (ids(rngid) % (nodesnum)) + 1;
            auto maxid = id - 1 > 0 ? id - 1 : nodesnum;
            while (true) {
                if (nodes[id]->successors().size() < 2) {
                    G.addNodeSuccessor(*nodes[id], nd);
                    edges.emplace_back(nodes[id], &nd);
                    assert(Enum != 0);
                    --Enum;
                    break;
                }
                if (id == maxid)
                    break;
                ++id;
                if (id > nodesnum)
                    id = 1;
            }
        } else if (Enum > 0) {
            auto nodesnum = (nodes.size() - 1);
            auto id1 = (ids(rngid) % nodesnum) + 1;
            while (nodes[id1]->successors().size() > 1) {
                ++id1;
                if (id1 > nodesnum)
                    id1 = 1;
            }
            auto id2 = (ids(rngid) % nodesnum) + 1;
            G.addNodeSuccessor(*nodes[id1], *nodes[id2]);
            //std::cout << id1 << " -> " << id2 << "\n";
            assert(Enum != 0);
            --Enum;
        } else if (Vnum > 0) {
            assert(Enum == 0);
            auto& nd = G.createNode();
            nodes.push_back(&nd);
            --Vnum;
        }
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
    if (irrcores > 0) {
        generateRandomIrreducibleGraph(G, irrcores, Vn, En);
    } else {
        generateRandomGraph(G, Vn, En);
    }

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
