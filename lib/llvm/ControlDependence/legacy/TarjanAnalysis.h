#ifndef DG_LEGACY_NTSCD_TARJANANALYSIS_H
#define DG_LEGACY_NTSCD_TARJANANALYSIS_H

#include <algorithm>
#include <queue>
#include <set>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dg {
namespace llvmdg {
namespace legacy {

// FIXME: we've got this and SCC, unify it
template <typename T>
class TarjanAnalysis {
  public:
    class StronglyConnectedComponent {
      private:
        static int idCounter;

      public:
        StronglyConnectedComponent() : id_(++idCounter) {}

        void addNode(T *node) { nodes_.push_back(node); }

        bool addSuccessor(StronglyConnectedComponent *successor) {
            if (!successor) {
                return false;
            }
            successors_.insert(successor);
            return successor->predecessors_.insert(this).second;
        }

        bool addPredecessor(StronglyConnectedComponent *predecessor) {
            if (!predecessor) {
                return false;
            }
            predecessors_.insert(predecessor);
            return predecessor->successors_.insert(this).second;
        }

        int id() const { return id_; }

        const std::vector<T *> &nodes() const { return nodes_; }

        const std::set<StronglyConnectedComponent *> &predecessors() const {
            return predecessors_;
        }

        const std::set<StronglyConnectedComponent *> &successors() const {
            return successors_;
        }

      private:
        int id_;
        std::vector<T *> nodes_;
        std::set<StronglyConnectedComponent *> successors_;
        std::set<StronglyConnectedComponent *> predecessors_;
    };

    // this we have to remember for each node
    struct Node {
        int dfsId{0};
        int lowLink{0};
        bool onStack{false};
        StronglyConnectedComponent *component{nullptr};
    };

    TarjanAnalysis(std::size_t size = 0) : nodeInfo(size) {}

    ~TarjanAnalysis() {
        for (auto component : components_) {
            delete component;
        }
    }

    void compute(T *currentNode) {
        ++index;
        nodeInfo[currentNode].dfsId = index;
        nodeInfo[currentNode].lowLink = index;

        stack.push(currentNode);
        nodeInfo[currentNode].onStack = true;

        for (auto *successor : currentNode->successors()) {
            if (!visited(successor)) {
                compute(successor);
                nodeInfo[currentNode].lowLink =
                        std::min(nodeInfo[currentNode].lowLink,
                                 nodeInfo[successor].lowLink);
            } else if (nodeInfo[successor].onStack) {
                nodeInfo[currentNode].lowLink =
                        std::min(nodeInfo[currentNode].lowLink,
                                 nodeInfo[successor].dfsId);
            }
        }

        if (nodeInfo[currentNode].lowLink == nodeInfo[currentNode].dfsId) {
            auto component = new StronglyConnectedComponent();
            components_.insert(component);

            T *node;
            while (nodeInfo[stack.top()].dfsId >= nodeInfo[currentNode].dfsId) {
                node = stack.top();
                stack.pop();
                nodeInfo[node].onStack = false;
                component->addNode(node);
                nodeInfo[node].component = component;

                if (stack.empty()) {
                    break;
                }
            }
        }
    }

    void computeCondensation() {
        for (auto component : components_) {
            for (auto *node : component->nodes()) {
                for (auto *successor : node->successors()) {
                    if (nodeInfo[node].component !=
                        nodeInfo[successor].component) {
                        nodeInfo[node].component->addSuccessor(
                                nodeInfo[successor].component);
                    }
                }
            }
        }
    }

    std::set<StronglyConnectedComponent *>
    computeBackWardReachability(T *node) {
        std::set<StronglyConnectedComponent *> visitedComponents;
        std::queue<StronglyConnectedComponent *> queue;

        auto iterator = nodeInfo.find(node);
        if (iterator == nodeInfo.end()) {
            return visitedComponents;
        }
        auto initialComponent = iterator.second;
        visitedComponents.insert(initialComponent);
        queue.push(initialComponent);

        while (!queue.empty()) {
            auto component = queue.front();
            queue.pop();
            for (auto predecessor : component->predecessors()) {
                if (visitedComponents.find(predecessor) ==
                    visitedComponents.end()) {
                    visitedComponents.insert(predecessor);
                    queue.push(predecessor);
                }
            }
        }
        return visitedComponents;
    }

    const std::set<StronglyConnectedComponent *> &components() const {
        return components_;
    }

  private:
    int index{0};

    std::stack<T *> stack;

    std::unordered_map<T *, Node> nodeInfo;

    std::set<StronglyConnectedComponent *> components_;

    bool visited(T *node) { return nodeInfo[node].dfsId > 0; }
};

template <typename T>
int TarjanAnalysis<T>::StronglyConnectedComponent::idCounter = 0;

} // namespace legacy
} // namespace llvmdg
} // namespace dg

#endif // DG_LLVM_TARJANANALYSIS_H
