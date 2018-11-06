#ifndef _DG_INVALIDATED_ANALYSIS_H_
#define _DG_INVALIDATED_ANALYSIS_H_

#include <set>
#include "PointerSubgraph.h"
#include "PSNode.h"

namespace dg {
namespace analysis {
namespace pta {

class InvalidatedAnalysis {
    struct State {
        std::set<PSNode *> mustBeInv{};
        std::set<PSNode *> mayBeInv{};
        /*
        std::set<PSNode *> mustBeNull;
        std::set<PSNode *> cannotBeNull;
        */
    };

    PointerSubgraph *PS{nullptr};

    // mapping from PSNode's ID to States
    std::vector<State *> _mapping;
    std::vector<std::unique_ptr<State>> _states;

    static inline bool isRelevantNode(PSNode *node) {
        return node->getType() == PSNodeType::STORE ||
               node->getType() == PSNodeType::ALLOC ||
               node->getType() == PSNodeType::DYN_ALLOC ||
               node->getType() == PSNodeType::INVALIDATE_LOCALS ||;
               node->getType() == PSNodeType::INVALIDATE_OBJECT;
    }

    static inline bool noChange(PSNode *node) {
        return node->predecessorsNum() == 1 &&
                !isRelevantNode(node);
    }

    State *newState(PSNode *nd) {
        assert(nd->getID() < _mapping.size());

        _states.emplace_back(new State());
        auto state = _states.back().get();
        _mapping[nd->getID()] = state;

        return state;
    }

    State *getState(PSNode *nd) {
        assert(nd->getID() < _mapping.size());
        return _mapping[nd->getID()];
    }

    static inline bool changesState(PSNode *node) {
        return node->predecessorsNum() == 0 ||
               node->predecessorsNum() > 1 ||
               isRelevantNode(node);
    }

    bool processNode(PSNode *node) {
        assert(node->getID() <_states.size());

        if (noChange(node)) {
            auto pred = node->getSinglePredecessor();
            assert(pred->getID() <_states.size());

            _mapping[node->getID()] = getState(pred);
            return false;
        }

        if (changesState(node) && !getState(node)) {
            newState(node);
        }

        for (PSNode *pred : node->getPredecessors()) {
        }

        return false;
    }

public:

    /// Pointer subgraph with computed pointers
    InvalidatedAnalysis(PointerSubgraph *ps)
    : PS(ps), _states(ps->size()) {}

    void run() {
        std::vector<PSNode *> to_process;
        std::vector<PSNode *> changed;

        to_process.resize(PS->size());
        for (auto& nd : PS->getNodes())
            to_process.push_back(nd.get());

        while(!to_process.empty()) {
            for (auto nd : to_process) {
                if (processNode(nd))
                    changed.push_back(nd);
            }

            to_process.swap(changed);
            changed.clear();
        }
    }

};

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_INVALIDATED_ANALYSIS_H_
