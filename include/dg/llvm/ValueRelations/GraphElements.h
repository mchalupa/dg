#ifndef DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_HPP_
#define DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_HPP_

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

    bool inLoop = false;

    ValueRelations relations;

    std::vector<VREdge *> predecessors;
    std::vector<std::unique_ptr<VREdge>> successors;

    VRLocation(unsigned _id) : id(_id) {}

    void connect(std::unique_ptr<VREdge> &&edge);

    void connect(VRLocation *target, std::unique_ptr<VROp> &&op);

    void connect(VRLocation *target, VROp *op);

    void connect(VRLocation &target, VROp *op);

    std::vector<VREdge *> getPredecessors();

    std::vector<VREdge *> getSuccessors();

    std::vector<VRLocation *> getPredLocations();

    std::vector<VRLocation *> getSuccLocations();

    bool isJoin() const;

    bool isJustBranchJoin() const;

    bool isJustLoopJoin() const;

    VRLocation &getTreePredecessor() const;

#ifndef NDEBUG
    void dump() const { std::cout << id << std::endl; }
#endif
};

class VRCodeGraph {
    friend struct GraphBuilder;

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
    VRLocation &getVRLocation(const llvm::Instruction *ptr) const;

    VRLocation &getEntryLocation(const llvm::Function *f) const;

    void hasCategorizedEdges();

    /* ************ function iterator stuff ************ */

  private:
    struct Supplements {
        enum class Dir { FORWARD, BACKWARD };

        Supplements() = default;
        Supplements(const llvm::Function *f, VRLocation *start, Dir d);

        std::vector<VREdge *> getNextEdges(VRLocation *loc) const;

        VRLocation *getNextLocation(VREdge *edge) const;

        bool inOtherFunction(VREdge *edge) const;

        // is null or target was visited or leads to other function
        bool irrelevant(VREdge *edge) const;

      protected:
        const llvm::Function *function;
        std::set<VRLocation *> visited;

        Dir direction;
    };

    struct BFSIncrement : protected Supplements {
      protected:
        BFSIncrement() = default;
        BFSIncrement(const llvm::Function *f, VRLocation *start, bool e, Dir d);

        void increment();

        const std::queue<std::pair<VRLocation *, VREdge *>> &structure() const {
            return queue;
        }
        VRLocation *location() const { return queue.front().first; }
        VREdge *edge() const { return queue.front().second; }

      private:
        bool categorizedEdges;
        std::map<VRLocation *, unsigned> counts;

        std::queue<std::pair<VRLocation *, VREdge *>> queue;
    };

    struct DFSIncrement : protected Supplements {
      protected:
        DFSIncrement() = default;
        DFSIncrement(const llvm::Function *f, VRLocation *start, bool /*e*/,
                     Dir d);

        void increment();

        const std::vector<std::tuple<VRLocation *, unsigned, VREdge *>> &
        structure() const {
            return stack;
        }
        VRLocation *location() const { return std::get<0>(stack.back()); }
        VREdge *edge() const { return std::get<2>(stack.back()); }

      public:
        bool onStack(VRLocation *loc) const;

        bool wasVisited(VRLocation *loc) const;

      private:
        std::vector<std::tuple<VRLocation *, unsigned, VREdge *>> stack;
    };

    template <typename Strategy>
    struct VRFunctionIterator : public Strategy {
        VRFunctionIterator() = default;
        VRFunctionIterator(const llvm::Function *f, VRLocation *start, bool e,
                           Supplements::Dir d)
                : Strategy(f, start, e, d) {}

        VRLocation &operator*() const { return *operator->(); }
        VRLocation *operator->() const { return this->location(); }

        // returns the edge on which to reach the current location
        VREdge *getEdge() const { return this->edge(); }

        friend bool operator==(const VRFunctionIterator &lt,
                               const VRFunctionIterator &rt) {
            return lt.structure() == rt.structure();
        }

        friend bool operator!=(const VRFunctionIterator &lt,
                               const VRFunctionIterator &rt) {
            return !(lt == rt);
        }

        VRFunctionIterator &operator++() {
            this->increment();
            return *this;
        }

        VRFunctionIterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }
    };

  public:
    using BFSIterator = VRFunctionIterator<BFSIncrement>;
    using DFSIterator = VRFunctionIterator<DFSIncrement>;
    using Dir = Supplements::Dir;

    BFSIterator bfs_begin(const llvm::Function *f) const {
        return BFSIterator(f, &getEntryLocation(f), categorizedEdges,
                           Dir::FORWARD);
    }

    BFSIterator bfs_end(const llvm::Function * /*f*/) const {
        return BFSIterator();
    }

    BFSIterator bfs_begin(const llvm::Function *f, VRLocation &start) const {
        return BFSIterator(f, &start, categorizedEdges, Dir::FORWARD);
    }

    BFSIterator bfs_end(const llvm::Function * /*f*/,
                        VRLocation & /*start*/) const {
        return BFSIterator();
    }

    BFSIterator backward_bfs_begin(const llvm::Function *f,
                                   VRLocation &start) const {
        return BFSIterator(f, &start, categorizedEdges, Dir::BACKWARD);
    }

    BFSIterator backward_bfs_end(const llvm::Function * /*f*/,
                                 VRLocation & /*start*/) const {
        return BFSIterator();
    }

    DFSIterator dfs_begin(const llvm::Function *f) const {
        return DFSIterator(f, &getEntryLocation(f), categorizedEdges,
                           Dir::FORWARD);
    }

    DFSIterator dfs_end(const llvm::Function * /*f*/) const {
        return DFSIterator();
    }

    /* ************ code graph iterator stuff ************ */

  private:
    struct VRCodeGraphIterator {
        using FunctionMapping = std::map<const llvm::Function *, VRLocation *>;
        using MappingIterator = typename FunctionMapping::const_iterator;

        VRCodeGraphIterator() = default;
        VRCodeGraphIterator(MappingIterator end);
        VRCodeGraphIterator(MappingIterator begin, MappingIterator end, bool e);

        VRLocation &operator*() { return *intoFunction; }
        VRLocation *operator->() { return &operator*(); }

        friend bool operator==(const VRCodeGraph::VRCodeGraphIterator &lt,
                               const VRCodeGraph::VRCodeGraphIterator &rt) {
            bool ltIsEnd = lt.intoMapping == lt.endMapping;
            bool rtIsEnd = rt.intoMapping == rt.endMapping;
            return (ltIsEnd && rtIsEnd) ||
                   (!ltIsEnd && !rtIsEnd && lt.intoFunction == rt.intoFunction);
        }

        friend bool operator!=(const VRCodeGraphIterator &lt,
                               const VRCodeGraphIterator &rt) {
            return !(lt == rt);
        }

        VRCodeGraphIterator &operator++();

        VRCodeGraphIterator operator++(int);

      private:
        MappingIterator intoMapping;
        MappingIterator endMapping;

        BFSIterator intoFunction;
        bool categorizedEdges;
    };

  public:
    using iterator = VRCodeGraphIterator;

    iterator begin() const {
        return functionMapping.empty()
                       ? iterator()
                       : iterator(functionMapping.begin(),
                                  functionMapping.end(), categorizedEdges);
    }

    iterator end() const {
        return functionMapping.empty() ? iterator()
                                       : iterator(functionMapping.end());
    }
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_GRAPH_ELEMENTS_HPP_
