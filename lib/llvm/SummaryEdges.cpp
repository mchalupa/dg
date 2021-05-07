#include <set>
#include <unordered_map>
#include <utility>

#include "dg/llvm/LLVMDependenceGraph.h"
#include "dg/llvm/LLVMNode.h"
//#include "llvm-utils.h"

#include "dg/ADT/Queue.h"

namespace dg {

/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph -- summary edges
/// ------------------------------------------------------------------

extern std::map<llvm::Value *, LLVMDependenceGraph *> constructedFunctions;
class SummaryEdgesComputation {
    using NodeT = LLVMNode;
    using Edge = std::pair<NodeT *, NodeT *>;
    ADT::QueueLIFO<Edge> workList;
    // FIXME: optimize this: we can store only a subset of these edges
    // and we can store only a set of nodes (beginnings of the paths)
    std::set<Edge> pathEdge;

    // collected vertices for fast checks
    std::set<NodeT *> actualOutVertices;
    std::set<NodeT *> formalInVertices;

    void propagate(const Edge &e) {
        if (pathEdge.insert(e).second) {
            workList.push(e);
        }
    }

    void propagate(NodeT *n1, NodeT *n2) { propagate({n1, n2}); }

    void initialize() {
        // collect all actual out and formal in vertices,
        // as we need to check whether a node is of this type
        for (auto &it : getConstructedFunctions()) {
            LLVMDependenceGraph *dg = it.second;
            assert(dg && "null as dg");

            // formal parameters of this dg
            if (auto params = dg->getParameters()) {
                for (auto &paramIt : *params) {
                    // Initialize the worklist (using formal out params).
                    propagate({paramIt.second.out, paramIt.second.out});
                    // Gather formal in params.
                    formalInVertices.insert(paramIt.second.in);
                }

                // XXX: what about params for globals?
            }

            for (auto callNode : dg->getCallNodes()) {
                // gather actual-out vertices
                if (auto params = callNode->getParameters()) {
                    for (auto &paramIt : *params) {
                        actualOutVertices.insert(paramIt.second.out);
                    }

                    // XXX: what about params for globals?
                }
            }
        }
    }

    bool isActualOut(NodeT *n) const { return actualOutVertices.count(n) > 0; }
    bool isFormalIn(NodeT *n) const { return formalInVertices.count(n) > 0; }

    void handleActualOut(const Edge &e) {
        for (auto I = e.first->rev_control_begin(),
                  E = e.first->rev_control_end();
             I != E; ++I) {
            propagate(*I, e.second);
        }
        for (auto I = e.first->rev_summary_begin(),
                  E = e.first->rev_summary_end();
             I != E; ++I) {
            propagate(*I, e.second);
        }
    }

    void handleGenericEdge(const Edge &e) {
        for (auto I = e.first->rev_control_begin(),
                  E = e.first->rev_control_end();
             I != E; ++I) {
            propagate(*I, e.second);
        }
        for (auto I = e.first->rev_data_begin(), E = e.first->rev_data_end();
             I != E; ++I) {
            propagate(*I, e.second);
        }
        for (auto I = e.first->user_begin(), E = e.first->user_end(); I != E;
             ++I) {
            propagate(*I, e.second);
        }
    }

    void handleFormalIn(const Edge &e) {
        using namespace llvm;

        // the edge 'e' is a summary edge between formal parameters,
        // we want to map it to actual parameters
        assert(e.first->getDG());
        assert(e.second->getDG());

        auto params =
                LLVMDGFormalParameters::get(e.second->getDG()->getParameters());
        assert(params);
        assert(params->formalToActual.find(e.second) !=
                       params->formalToActual.end() &&
               "Cannot find formal -> actual parameter mapping");
        for (auto &it : params->formalToActual[e.second]) {
            // it = pair of (call-site, actual (out) param)
            // get the actual in param too }
            assert(params->formalToActual.find(e.first) !=
                   params->formalToActual.end());
            auto actIn = params->formalToActual[e.first][it.first];
            assert(actIn && "Do not have actual in param");

            // add summary edge
            actIn->addSummaryEdge(it.second);

            // XXX: very inefficient
            for (auto &pe : pathEdge) {
                if (pe.first == it.second) {
                    propagate(actIn, pe.second);
                }
            }
        }
    }

  public:
    void computeSummaryEdges() {
        initialize();

        while (!workList.empty()) {
            Edge e = workList.pop();

            if (isActualOut(e.first)) {
                handleActualOut(e);
            } else if (isFormalIn(e.first)) {
                handleFormalIn(e);
            } else {
                handleGenericEdge(e);
            }
        }
    }
};

void LLVMDependenceGraph::computeSummaryEdges() {
    SummaryEdgesComputation C;
    C.computeSummaryEdges();
}
} // namespace dg
