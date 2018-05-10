#ifndef _DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_
#define _DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_

namespace dg {
namespace analysis {
namespace pta {

class PSNoopRemover {
    PointerSubgraph *PS;
public:
    PSNoopRemover(PointerSubgraph *PS) : PS(PS) {}

    void run() {
        for (PSNode *nd : PS->getNodes()) {
            if (!nd)
                continue;

            if (nd->getType() == PSNodeType::NOOP) {
                nd->isolate();
                // this should not break the iterator
                PS->remove(nd);
            }
        }
    };
};

class PointerSubgraphOptimizer {
    PointerSubgraph *PS;
public:
    PointerSubgraphOptimizer(PointerSubgraph *PS) : PS(PS) {}

    void removeNoops() {
        PSNoopRemover remover(PS);
        remover.run();
    }
};


} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_POINTER_SUBGRAPH_OPTIMIZATIONS_H_
