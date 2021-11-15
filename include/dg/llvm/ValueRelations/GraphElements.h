#ifndef DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_H_
#define DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_H_

#include <cassert>
#include <list>
#include <queue>

#include "UniquePtrVector.h"
#include "ValueRelations.h"

#include <llvm/IR/Instructions.h>

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

    void generalDump(std::ostream &stream) const { stream << toStr(); }

    void dump() const { generalDump(std::cout); }
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

enum class EdgeType {
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

    ValueRelations relations;

    std::vector<VREdge *> predecessors;
    std::vector<std::unique_ptr<VREdge>> successors;

    std::vector<const VREdge *> loopEnds;
    const VRLocation *join = nullptr;

    VRLocation(unsigned _id) : id(_id) {}

    void connect(std::unique_ptr<VREdge> &&edge);
    void connect(VRLocation *target, std::unique_ptr<VROp> &&op);
    void connect(VRLocation *target, VROp *op);
    void connect(VRLocation &target, VROp *op);

    unsigned predsSize() const { return predecessors.size(); }
    unsigned succsSize() const { return successors.size(); }

    VREdge *getPredEdge(unsigned i) const { return predecessors[i]; }
    VREdge *getSuccEdge(unsigned i) const { return successors[i].get(); }

    VRLocation *getPredLocation(unsigned i) const {
        return predecessors[i]->source;
    }
    VRLocation *getSuccLocation(unsigned i) const {
        return successors[i]->target;
    }

    std::vector<VRLocation *> getPredLocations();
    std::vector<VRLocation *> getSuccLocations();

    bool isJoin() const;
    bool isJustBranchJoin() const;
    bool isJustLoopJoin() const;

    VRLocation &getTreePredecessor() const;

#ifndef NDEBUG
    void dump() const { std::cout << id << "\n"; }
#endif
};

class VRCodeGraph {
    friend class GraphBuilder;

    UniquePtrVector<VRLocation> locations;
    std::map<const llvm::Function *, VRLocation *> functionMapping;
    // VRLocation corresponding to the state of the program BEFORE executing the
    // instruction
    std::map<const llvm::Instruction *, VRLocation *> locationMapping;

    bool categorizedEdges = false;

    VRLocation &newVRLocation();
    VRLocation &newVRLocation(const llvm::Instruction *inst);
    void setEntryLocation(const llvm::Function *f, VRLocation &loc);

  public:
    /* return VRLocation corresponding to the state of the program BEFORE
     * executing the passed instruction */
    VRLocation &getVRLocation(const llvm::Instruction *ptr) const;
    VRLocation &getEntryLocation(const llvm::Function &f) const;

    void hasCategorizedEdges();

    /* ************ function iterator stuff ************ */

  private:
    class SimpleVisit {
        std::set<VRLocation *> visited;

      protected:
        void find(VRLocation *loc);
        static bool shouldVisit(__attribute__((unused)) VRLocation *loc) {
            return true;
        }

      public:
        bool wasVisited(VRLocation *loc) const;
    };

    class LazyVisit {
        std::map<VRLocation *, unsigned> visited;

        static unsigned getPrevEdgesSize(VRLocation *loc);

      protected:
        void find(VRLocation *loc);
        bool shouldVisit(VRLocation *loc) const {
            return visited.find(loc)->second >= getPrevEdgesSize(loc);
        }

      public:
        bool wasVisited(VRLocation *loc) const;
    };

    enum class Dir { FORWARD, BACKWARD };

    template <typename Visit>
    class DFSIt : public Visit {
        const llvm::Function *function;
        std::vector<std::tuple<VRLocation *, unsigned, VREdge *>> stack;
        Dir dir = Dir::FORWARD;

        VREdge *getNextEdge(VRLocation *loc, unsigned i) const {
            return dir == Dir::FORWARD ? loc->getSuccEdge(i)
                                       : loc->getPredEdge(i);
        }
        VRLocation *getNextLocation(VREdge *edge) const {
            return dir == Dir::FORWARD ? edge->target : edge->source;
        }

        bool inOtherFunction(VREdge *edge) const {
            if (edge->op->isInstruction()) {
                const llvm::Instruction *inst =
                        static_cast<VRInstruction *>(edge->op.get())
                                ->getInstruction();
                if (inst->getFunction() != function) {
                    assert(0 && "has edge to other function");
                    return true;
                }
            }
            return false;
        }
        // is null or target was visited or leads to other function
        bool isIrrelevant(VREdge *edge) const {
            VRLocation *next = getNextLocation(edge);
            return !next || Visit::wasVisited(next) || inOtherFunction(edge);
        }

        void visit(VRLocation *loc) {
            while (!Visit::wasVisited(loc))
                Visit::find(loc);
        }

      public:
        DFSIt() = default;
        DFSIt(const llvm::Function &f, const VRLocation *start, Dir d)
                : function(&f), dir(d) {
            stack.emplace_back(const_cast<VRLocation *>(start), 0, nullptr);
            visit(const_cast<VRLocation *>(start));
        }

        friend bool operator==(const DFSIt &lt, const DFSIt &rt) {
            return lt.stack == rt.stack;
        }
        friend bool operator!=(const DFSIt &lt, const DFSIt &rt) {
            return !(lt == rt);
        }

        DFSIt &operator++() {
            while (!stack.empty()) {
                VRLocation *current;
                unsigned index;
                VREdge *prevEdge;
                std::tie(current, index, prevEdge) = stack.back();
                stack.pop_back();

                unsigned nextSize = dir == Dir::FORWARD ? current->succsSize()
                                                        : current->predsSize();
                // do not explore if there is no target or if target was already
                // explored or if is in other function

                while (index < nextSize &&
                       isIrrelevant(getNextEdge(current, index)))
                    ++index;

                if (index >= nextSize)
                    continue;
                stack.emplace_back(current, index + 1, prevEdge);

                VREdge *nextEdge = getNextEdge(current, index);
                VRLocation *next = getNextLocation(nextEdge);

                Visit::find(next);
                if (Visit::shouldVisit(next)) {
                    stack.emplace_back(next, 0, nextEdge);
                    break;
                }
            }
            return *this;
        }

        VRLocation &operator*() const { return *std::get<0>(stack.back()); }
        VRLocation *operator->() const { return std::get<0>(stack.back()); }

        bool onStack(VRLocation *loc) const {
            for (const auto &elem : stack)
                if (loc == std::get<0>(elem))
                    return true;
            return false;
        }
        VREdge *getEdge() const { return std::get<2>(stack.back()); }

        void skipSuccessors() { stack.pop_back(); }
    };

  public:
    using SimpleDFS = DFSIt<SimpleVisit>;
    using LazyDFS = DFSIt<LazyVisit>;

    LazyDFS lazy_dfs_begin(const llvm::Function &f) const;
    LazyDFS lazy_dfs_begin(const llvm::Function &f,
                           const VRLocation &start) const;
    static LazyDFS lazy_dfs_end();

    SimpleDFS dfs_begin(const llvm::Function &f) const;
    SimpleDFS dfs_begin(const llvm::Function &f, const VRLocation &start) const;
    static SimpleDFS dfs_end();

    static SimpleDFS backward_dfs_begin(const llvm::Function &f,
                                        const VRLocation &start);
    static SimpleDFS backward_dfs_end();

    /* ************ code graph iterator stuff ************ */

    struct VRCodeGraphIterator {
        using FunctionMapping = std::map<const llvm::Function *, VRLocation *>;
        using MappingIterator = typename FunctionMapping::const_iterator;

        VRCodeGraphIterator() = default;
        VRCodeGraphIterator(MappingIterator end);
        VRCodeGraphIterator(MappingIterator begin, MappingIterator end);

        VRLocation &operator*() { return *intoFunction; }
        VRLocation *operator->() { return &operator*(); }

        friend bool operator==(const VRCodeGraphIterator &lt,
                               const VRCodeGraphIterator &rt);
        friend bool operator!=(const VRCodeGraphIterator &lt,
                               const VRCodeGraphIterator &rt) {
            return !(lt == rt);
        }

        VRCodeGraphIterator &operator++();
        VRCodeGraphIterator operator++(int);

      private:
        MappingIterator intoMapping;
        MappingIterator endMapping;

        LazyDFS intoFunction;
    };

    VRCodeGraphIterator begin() const;
    VRCodeGraphIterator end() const;
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_H_
