#ifndef DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_HPP_
#define DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_HPP_

#include <cassert>
#include <queue>
#include <list>

#include <llvm/IR/Instructions.h>

#include "UniquePtrVector.h"
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

    void generalDump(std::ostream &stream) { stream << toStr(); }

    void dump() { generalDump(std::cout); }
#endif
};

struct VRNoop : public VROp {
    VRNoop() : VROp(VROpType::NOOP) {}

#ifndef NDEBUG
    std::string toStr() const override { return "(noop)"; }
#endif
};

struct VRInstruction : public VROp {
    const llvm::Instruction *instruction;

    VRInstruction(const llvm::Instruction *I)
            : VROp(VROpType::INSTRUCTION), instruction(I) {}

    VRInstruction(const llvm::Instruction &I) : VRInstruction(&I) {}

    const llvm::Instruction *getInstruction() const { return instruction; }

#ifndef NDEBUG
    std::string toStr() const override {
        return debug::getValName(instruction);
    }
#endif
};

struct VRAssume : public VROp {
    const llvm::Value *val;

    const llvm::Value *getValue() const { return val; }

  protected:
    VRAssume(VROpType type, const llvm::Value *v) : VROp(type), val(v) {}

#ifndef NDEBUG
    std::string toStr() const override {
        return "assuming " + debug::getValName(val) + " is ";
    }
#endif
};

struct VRAssumeBool : public VRAssume {
    bool assumption;

    VRAssumeBool(const llvm::Value *v, bool b)
            : VRAssume(VROpType::ASSUME_BOOL, v), assumption(b) {}

    bool getAssumption() const { return assumption; }

#ifndef NDEBUG
    std::string toStr() const override {
        return VRAssume::toStr() + (assumption ? "true" : "false");
    }
#endif
};

struct VRAssumeEqual : public VRAssume {
    const llvm::Value *assumption;

    VRAssumeEqual(const llvm::Value *v, const llvm::Value *a)
            : VRAssume(VROpType::ASSUME_EQUAL, v), assumption(a) {}

    const llvm::Value *getAssumption() const { return assumption; }

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

    VREdge(VRLocation *s, VRLocation *t, std::unique_ptr<VROp> &&op)
            : source(s), target(t), op(std::move(op)) {}

    VREdge(VRLocation *s, VRLocation *t, VROp *opRaw)
            : source(s), target(t), op(opRaw) {}
};

struct VRLocation {
    const unsigned id;

    bool inLoop = false;

    ValueRelations relations;

    std::vector<VREdge *> predecessors;
    std::vector<std::unique_ptr<VREdge>> successors;

    VRLocation(unsigned _id) : id(_id) {}

    void connect(std::unique_ptr<VREdge> &&edge) {
        if (edge->target)
            edge->target->predecessors.push_back(edge.get());
        successors.emplace_back(std::move(edge));
    }

    void connect(VRLocation *target, std::unique_ptr<VROp> &&op) {
        connect(std::unique_ptr<VREdge>(
                new VREdge(this, target, std::move(op))));
    }

    void connect(VRLocation *target, VROp *op) {
        connect(std::unique_ptr<VREdge>(new VREdge(this, target, op)));
    }

    void connect(VRLocation &target, VROp *op) { connect(&target, op); }

    std::vector<VREdge *> getPredecessors() { return predecessors; }

    std::vector<VREdge *>
    getSuccessors() { // TODO create an iterator to unwrap the unique pointers
        std::vector<VREdge *> result;
        for (auto &succ : successors) {
            result.push_back(succ.get());
        }
        return result;
    }

    std::vector<VRLocation *> getPredLocations() {
        std::vector<VRLocation *> result;
        for (VREdge *edge : predecessors) {
            result.push_back(edge->source);
        }
        return result;
    }

    std::vector<VRLocation *> getSuccLocations() {
        std::vector<VRLocation *> result;
        for (auto &edge : successors) {
            result.push_back(edge->target);
        }
        return result;
    }

    bool isJoin() const { return predecessors.size() > 1; }

    bool isJustBranchJoin() const {
        // allows TREE and FORWARD
        if (!isJoin())
            return false;
        for (VREdge *pred : predecessors) {
            if (pred->type == EdgeType::BACK)
                return false;
        }
        return true;
    }

    bool isJustLoopJoin() const {
        // allows TREE and BACK
        if (!isJoin())
            return false;
        for (VREdge *pred : predecessors) {
            if (pred->type == EdgeType::FORWARD)
                return false;
        }
        return true;
    }

#ifndef NDEBUG
    void dump() const { std::cout << id << std::endl; }
#endif
};

struct VRCodeGraph {
    friend struct GB;
    friend struct VRCodeGraphIterator;

    using VRBBlock = UniquePtrVector<VRLocation>;
    using VRBBlockHandle = unsigned;

    std::vector<VRBBlock> vrblocks;
    unsigned totalLocations = 0;

    std::vector<const llvm::Function *> functions;
    std::map<const llvm::BasicBlock *, VRBBlockHandle> blockMapping;
    // VRLocation corresponding to the state of the program BEFORE executing the
    // instruction
    std::map<const llvm::Instruction *, VRLocation *> locationMapping;

    bool categorizedEdges = false;

    VRBBlockHandle newVRBBlock() {
        vrblocks.emplace_back();
        return vrblocks.size() - 1;
    }

    VRBBlockHandle newVRBBlock(const llvm::BasicBlock *b) {
        assert(blockMapping.find(b) == blockMapping.end());

        VRBBlockHandle block = newVRBBlock();
        blockMapping.emplace(b, block);
        return block;
    }

    VRLocation &newVRLocation(VRBBlockHandle vrblock) {
        vrblocks[vrblock].emplace_back(++totalLocations);
        return vrblocks[vrblock].back();
    }

    VRLocation &newVRLocation(VRBBlockHandle vrblock,
                              const llvm::Instruction *inst) {
        assert(locationMapping.find(inst) == locationMapping.end());

        VRLocation &loc = newVRLocation(vrblock);
        locationMapping.emplace(inst, &loc);
        return loc;
    }

    VRLocation &insertAfter(const llvm::BasicBlock *b) {
        VRBBlockHandle h = getVRBBlockHandle(b);
        VRBBlockHandle newH = h + 1;

        vrblocks.emplace(vrblocks.begin() + newH);
        return newVRLocation(newH);
    }

    void addFunction(const llvm::Function *f) { functions.emplace_back(f); }

  public:
    VRBBlockHandle getVRBBlockHandle(const llvm::BasicBlock *b) const {
        return blockMapping.at(b);
    }

    const VRBBlock &getVRBBlock(const llvm::BasicBlock *b) const {
        return vrblocks[getVRBBlockHandle(b)];
    }

    VRLocation &getVRLocation(const llvm::Instruction *ptr) const {
        return *locationMapping.at(ptr);
    }

    VRLocation &getEntryLocation(const llvm::Function *f) const {
        return getVRBBlock(&f->getEntryBlock()).front();
    }

    struct VRCodeGraphIterator {
        using value_type = VRLocation;
        using difference_type = uint64_t;
        using reference = value_type &;
        using pointer = value_type *;
        using iterator_category = std::forward_iterator_tag;

        VRCodeGraphIterator() = default;
        VRCodeGraphIterator(const std::vector<VRBBlock> &c, bool begin)
                : // VRCodeGraphIterator(const VRCodeGraph& o):
                  toBlock(begin ? c.begin() : c.end()), endBlock(c.end()),
                  toLocation(c.begin()->begin())
        // functions(f),
        // vrblocks(b),
        // owner(o),
        // visited(b.size()),
        // queue{
        // o.getVRBBlockHandle(&o.functions[currentFunction]->getEntryBlock()) },
        // toLocation(o.vrblocks[queue.front()].begin())
        {
            // visited[queue.front()] = true;
        }

        reference operator*() const { return *toLocation; }
        pointer operator->() const { return &operator*(); }

        friend bool operator==(const VRCodeGraphIterator &lt,
                               const VRCodeGraphIterator &rt) {
            if (lt.toBlock == lt.endBlock)
                return rt.toBlock == rt.endBlock;
            if (rt.toBlock == rt.endBlock)
                return false;
            return lt.toLocation == rt.toLocation;
            // return (lt.toBlock != lt.endBlock || rt.toBlock != rt.endBlock)
            //    || lt.toLocation == rt.toLocation;
        }

        friend bool operator!=(const VRCodeGraphIterator &lt,
                               const VRCodeGraphIterator &rt) {
            return !(lt == rt);
        }

        VRCodeGraphIterator &operator++() {
            ++toLocation;
            if (toLocation == toBlock->end()) {
                ++toBlock;
                if (toBlock != endBlock) {
                    toLocation = toBlock->begin();
                    assert(toLocation != toBlock->end());
                }
            }

            return *this;
        }

        /*VRCodeGraphIterator& operator++() {
            ++toLocation;

            if (toLocation == o.vrblocks[queue.front()].end()) {
                const VRBBlock& block = o.vrblocks[queue.front()].end();
                for (VREdge* edge : block.back().getSuccLocations())
            }
        }*/

        VRCodeGraphIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

      private:
        // const VRCodeGraph& owner;
        // const std::vector<const llvm::Function*>& functions;
        // unsigned currentFunction = 0;

        // const std::vector<VRBBlock>& vrblocks;
        // std::vector<bool> visited;

        // std::queue<VRBBlockHandle> queue;

        typename std::vector<VRBBlock>::const_iterator toBlock;
        typename std::vector<VRBBlock>::const_iterator endBlock;
        typename VRBBlock::iterator toLocation;
    };

    using iterator = VRCodeGraphIterator;

    iterator begin() const {
        return vrblocks.empty() ? iterator() : iterator(vrblocks, true);
    }
    iterator end() const {
        return vrblocks.empty() ? iterator() : iterator(vrblocks, false);
    }

#ifndef NDEBUG
    void dump() {
        for (VRBBlock &vr : vrblocks) {
            for (VRLocation &loc : vr) {
                loc.dump();
            }
            std::cout << std::endl;
        }
    }
#endif
};

struct VRBBlock {
    std::list<std::unique_ptr<VRLocation>> locations;

    void prepend(VRLocation *loc) { locations.emplace(locations.begin(), loc); }

    void append(VRLocation *loc) { locations.emplace_back(loc); }

    VRLocation *last() { return locations.back().get(); }
    VRLocation *first() { return locations.front().get(); }
    const VRLocation *last() const { return locations.back().get(); }
    const VRLocation *first() const { return locations.front().get(); }

    auto begin() -> decltype(locations.begin()) { return locations.begin(); }
    auto end() -> decltype(locations.end()) { return locations.end(); }
    auto begin() const -> decltype(locations.begin()) {
        return locations.begin();
    }
    auto end() const -> decltype(locations.end()) { return locations.end(); }
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_HPP_
