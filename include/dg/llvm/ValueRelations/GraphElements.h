#ifndef DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_HPP_
#define DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_HPP_

#include <list>
#include <llvm/IR/Instructions.h>

#include "ValueRelations.h"

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace dg {
namespace vr {

class VROp {
protected:
    enum class VROpType { NOOP, INSTRUCTION, ASSUME_BOOL, ASSUME_EQUAL } type;
    VROp(VROpType t) : type(t) {}

public:
    bool isNoop() const { return type == VROpType::NOOP; }
    bool isInstruction() const { return type == VROpType::INSTRUCTION; }
    bool isAssume() const { return isAssumeBool() || isAssumeEqual(); }
    bool isAssumeBool() const { return type == VROpType::ASSUME_BOOL; }
    bool isAssumeEqual() const { return type == VROpType::ASSUME_EQUAL; }

    virtual ~VROp() = default;

#ifndef NDEBUG
    virtual std::string toStr() const = 0;
    
    void generalDump(std::ostream& stream) {
        stream << toStr();
    }

    void dump() {
        generalDump(std::cout);
    }
#endif
};

struct VRNoop : public VROp {
    VRNoop() : VROp(VROpType::NOOP) {}

#ifndef NDEBUG
    std::string toStr() const override {
        return "(noop)";
    }
#endif
};

struct VRInstruction : public VROp {
    const llvm::Instruction* instruction;

    VRInstruction(const llvm::Instruction* I)
    : VROp(VROpType::INSTRUCTION), instruction(I) {}

    const llvm::Instruction* getInstruction() const { return instruction; }

#ifndef NDEBUG
    std::string toStr() const override {
        return debug::getValName(instruction);
    }
#endif
};

struct VRAssume : public VROp {
    const llvm::Value* val;

    const llvm::Value* getValue() const {
        return val;
    }

protected:
    VRAssume(VROpType type, const llvm::Value* v) : VROp(type), val(v) {}

#ifndef NDEBUG
    std::string toStr() const override {
        return "assuming " + debug::getValName(val) + " is ";
    }
#endif
};

struct VRAssumeBool : public VRAssume {
    bool assumption;

    VRAssumeBool(const llvm::Value* v, bool b)
        : VRAssume(VROpType::ASSUME_BOOL, v), assumption(b) {}

    bool getAssumption() const {
        return assumption;
    }

#ifndef NDEBUG
    std::string toStr() const override {
        return VRAssume::toStr() + (assumption ? "true" : "false");
    }
#endif
};

struct VRAssumeEqual : public VRAssume {
    const llvm::Value* assumption;

    VRAssumeEqual(const llvm::Value* v, const llvm::Value* a)
        : VRAssume(VROpType::ASSUME_EQUAL, v), assumption(a) {}

    const llvm::Value* getAssumption() const {
        return assumption;
    }

#ifndef NDEBUG
    std::string toStr() const override {
        return VRAssume::toStr() + debug::getValName(assumption);
    }
#endif
};

struct VRLocation;

enum EdgeType {
    TREE,
    BACK,
    FORWARD,
    DEFAULT
    // DANGER ignores cross
};

struct VREdge {
    VRLocation *source;
    VRLocation *target;

    std::unique_ptr<VROp> op;

    EdgeType type = EdgeType::DEFAULT;

    VREdge(VRLocation *s, VRLocation *t, std::unique_ptr<VROp>&& op)
    : source(s), target(t), op(std::move(op)) {}
};

struct VRLocation  {
    const unsigned id;

    bool inLoop = false;

    ValueRelations relations;

    std::vector<VREdge *> predecessors;
    std::vector<std::unique_ptr<VREdge>> successors;

    VRLocation(unsigned _id) : id(_id) {}

    void connect(std::unique_ptr<VREdge>&& edge) {
        if (edge->target)
            edge->target->predecessors.push_back(edge.get());
        successors.emplace_back(std::move(edge));
    }

    std::vector<VREdge *> getPredecessors() {
        return predecessors;
    }

    std::vector<VREdge *> getSuccessors() { // TODO create an iterator to unwrap the unique pointers
        std::vector<VREdge*> result;
        for (auto& succ : successors) {
            result.push_back(succ.get());
        }
        return result;
    }

    std::vector<VRLocation*> getPredLocations() {
        std::vector<VRLocation*> result;
        for (VREdge * edge : predecessors) {
            result.push_back(edge->source);
        }
        return result;
    }

    std::vector<VRLocation*> getSuccLocations() {
        std::vector<VRLocation*> result;
        for (auto& edge : successors) {
            result.push_back(edge->target);
        }
        return result;
    }

    bool isJoin() const {
        return predecessors.size() > 1;
    }

    bool isJustBranchJoin() const {
        // allows TREE and FORWARD
        if (!isJoin()) return false;
        for (VREdge* pred : predecessors) {
            if (pred->type == EdgeType::BACK)
                return false;
        }
        return true;
    }

    bool isJustLoopJoin() const {
        // allows TREE and BACK
        if (!isJoin()) return false;
        for (VREdge* pred : predecessors) {
            if (pred->type == EdgeType::FORWARD)
                return false;
        }
        return true;
    }

#ifndef NDEBUG
    void dump() const {
        std::cout << id << std::endl;
    }
#endif
};

struct VRBBlock {
    std::list<std::unique_ptr<VRLocation>> locations;

    void prepend(VRLocation* loc) {
        locations.emplace(locations.begin(), loc);
    }

    void append(VRLocation* loc) {
        locations.emplace_back(loc);
    }

    VRLocation *last() { return locations.back().get(); }
    VRLocation *first() { return locations.front().get(); }
    const VRLocation *last() const { return locations.back().get(); }
    const VRLocation *first() const { return locations.front().get(); }

    auto begin() -> decltype(locations.begin()) { return locations.begin(); }
    auto end()   -> decltype(locations.end())   { return locations.end(); }
    auto begin() const -> decltype(locations.begin()) { return locations.begin(); }
    auto end() const   -> decltype(locations.end())   { return locations.end(); }
};

} // namespace vr
} // namespace dg

#endif //DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_HPP_
