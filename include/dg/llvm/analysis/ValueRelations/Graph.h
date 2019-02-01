#ifndef _DG_LLVM_VALUE_RELATIONS_GRAPH_H_
#define _DG_LLVM_VALUE_RELATIONS_GRAPH_H_

#include <list>
#include <llvm/IR/Instructions.h>

#include "dg/analysis/ValueRelations/ValueRelations.h"

#include "Graph.h"
#include "Relations.h"
#include "EqualityMap.h"
#include "ReadsMap.h"

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace dg {
namespace analysis {

class VROp {
protected:
    enum class VROpType { INSTRUCTION, ASSUME, NOOP } _type;
    VROp(VROpType t) : _type(t) {}

public:
    bool isInstruction() const { return _type == VROpType::INSTRUCTION; }
    bool isAssume() const { return _type == VROpType::ASSUME; }
    bool isNoop() const { return _type == VROpType::NOOP; }

    virtual ~VROp() {}

#ifndef NDEBUG
    virtual void dump() const = 0;
#endif
};

struct VRNoop : public VROp {
    VRNoop() : VROp(VROpType::NOOP) {}

#ifndef NDEBUG
    void dump() const override {
        std::cout << "(noop)";
    }
#endif
};

struct VRInstruction : public VROp {
    const llvm::Instruction *instruction;
    VRInstruction(const llvm::Instruction *I)
    : VROp(VROpType::INSTRUCTION), instruction(I) {}

    const llvm::Instruction* getInstruction() const { return instruction; }

    static VRInstruction *get(VROp *op) {
        return op->isInstruction() ? static_cast<VRInstruction *>(op) : nullptr;
    }

#ifndef NDEBUG
    void dump() const override {
        std::cout << debug::getValName(instruction);
    }
#endif
};

struct VRAssume : public VROp {
    RelationsMap relations{};

    VRAssume() : VROp(VROpType::ASSUME) {}

    VRAssume(const VRRelation& rel)
    : VROp(VROpType::ASSUME) {
        relations.add(rel);
    }

    void addRelation(const VRRelation& rel) { relations.add(rel); }
    const RelationsMap& getRelations() const { return relations; }

    static VRAssume *get(VROp *op) {
        return op->isAssume() ? static_cast<VRAssume *>(op) : nullptr;
    }

#ifndef NDEBUG
    void dump() const override {
        std::cout << "[";
        relations.dump();
        std::cout << "]";
    }
#endif
};

struct VRLocation;
struct VREdge {
    VRLocation *source;
    VRLocation *target;

    std::unique_ptr<VROp> op;

    VREdge(VRLocation *s, VRLocation *t, std::unique_ptr<VROp>&& op)
    : source(s), target(t), op(std::move(op)) {}
};

struct VRLocation  {
    const unsigned id;

    // Valid equalities at this location
    EqualityMap<const llvm::Value *> equalities;
    // pairs (a,b) such that if we meet "load a", we know
    // the result is b
    ReadsMap reads;
    RelationsMap relations;

    std::vector<VREdge *> predecessors{};
    std::vector<std::unique_ptr<VREdge>> successors{};

    void addEdge(std::unique_ptr<VREdge>&& edge) {
        edge->target->predecessors.push_back(edge.get());
        successors.emplace_back(std::move(edge));
    }

    void transitivelyClose() {
        // add all equalities into relations
        for (auto& it : equalities) {
            for (auto& it2 : *(it.second.get()))
                relations.add(VRRelation::Eq(it.first, it2));
        }

        relations.transitivelyClose();
    }

    VRLocation(unsigned _id) : id(_id) {}

#ifndef NDEBUG
    void dump() const {
        std::cout << id << " ";
        std::cout << std::endl;
    }
#endif
};

struct VRBBlock {
    std::list<std::unique_ptr<VRLocation>> locations;

    void prepend(std::unique_ptr<VRLocation>&& loc) {
        locations.push_front(std::move(loc));
    }

    void append(std::unique_ptr<VRLocation>&& loc) {
        locations.push_back(std::move(loc));
    }

    VRLocation *last() { return locations.back().get(); }
    VRLocation *first() { return locations.front().get(); }
    const VRLocation *last() const { return locations.back().get(); }
    const VRLocation *first() const { return locations.front().get(); }
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_VALUE_RELATIONS_GRAPH_H_
