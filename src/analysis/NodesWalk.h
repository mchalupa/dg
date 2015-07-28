#ifndef _DG_NODES_WALK_H_
#define _DG_NODES_WALK_H_

#include "Analysis.h"

namespace dg {
namespace analysis {

static unsigned int walk_run_counter;

template <typename NodeT, typename QueueT>
class NodesWalk : public Analysis<NodeT>
{
public:
    template <typename FuncT, typename DataT>
    void walk(NodeT *entry, FuncT func, DataT data,
              bool control = true, bool deps = true)
    {
        unsigned int run_id = ++walk_run_counter;
        QueueT queue;

        assert(entry && "Need entry node for traversing nodes");

        AnalysesAuxiliaryData& aad = this->getAnalysisData(entry);
        aad.lastwalkid = run_id;

        queue.push(entry);

        while (!queue.empty()) {
            NodeT *n = queue.front();
            queue.pop();

            prepare(n);
            func(n, data);

            // add unprocessed vertices
            if (control)
                processEdges(n->control_begin(),
                             n->control_end(), queue, run_id);

            if (deps)
                processEdges(n->data_begin(),
                             n->data_end(), queue, run_id);
        }
    }

protected:
    // function that will be called for all the nodes,
    // but is defined by the analysis framework, not
    // by the analysis itself. For example it may
    // assign DFS order numbers
    virtual void prepare(NodeT *n)
    {
    }

private:
    template <typename IT>
    void processEdges(IT begin, IT end, QueueT& queue,
                      unsigned int run_id)
    {
        for (IT I = begin; I != end; ++I) {
            NodeT *tmp = *I;
            AnalysesAuxiliaryData& aad = this->getAnalysisData(tmp);

            if (aad.lastwalkid == run_id)
                continue;

            // mark node as visited
            aad.lastwalkid = run_id;
            queue.push(tmp);
        }
    }

    QueueT queue;
};

#ifdef ENABLE_CFG
template <typename NodeT, typename QueueT>
class BBlockWalk : public BBlockAnalysis<NodeT>
{
public:
    typedef dg::BBlock<NodeT> *BBlockPtrT;

    template <typename FuncT, typename DataT>
    void walk(BBlockPtrT entry, FuncT func, DataT data)
    {
        QueueT queue;
        queue.push(entry);

        // set up the value of new walk and set it to entry node
        unsigned int runid = ++walk_run_counter;
        AnalysesAuxiliaryData& aad = this->getAnalysisData(entry);
        aad.lastwalkid = runid;

        while (!queue.empty()) {
            BBlockPtrT BB = queue.front();
            queue.pop();

            prepare(BB);
            func(BB, data);

            for (BBlockPtrT S : BB->successors()) {
                AnalysesAuxiliaryData& sad = this->getAnalysisData(S);
                if (sad.lastwalkid != runid) {
                    sad.lastwalkid = runid;
                    queue.push(S);
                }
            }
        }
    }

protected:
    virtual void prepare(BBlockPtrT BB)
    {
    }
};

#endif

} // namespace analysis
} // namespace dg

#endif // _DG_NODES_WALK_H_
