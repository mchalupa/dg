#ifndef _DG_LLVM_VALUE_RELATION_ANALYSIS_H_
#define _DG_LLVM_VALUE_RELATION_ANALYSIS_H_

#include <list>

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/CFG.h>

#include "dg/analysis/ValueRelations/ValueRelations.h"

namespace dg {
namespace analysis {

namespace detail {
static inline std::string getValName(const llvm::Value *val) {
    std::ostringstream ostr;
    llvm::raw_os_ostream ro(ostr);

    assert(val);
    ro << *val;
    ro.flush();

    // break the string if it is too long
    return ostr.str();
}
} // namespace detail

class VROp {
protected:
    enum class VROpType { INSTRUCTION, ASSUME, NOOP } _type;
    VROp(VROpType t) : _type(t) {}

public:
    bool isInstruction() const { return _type == VROpType::INSTRUCTION; }
    bool isAssume() const { return _type == VROpType::ASSUME; }
    bool isNoop() const { return _type == VROpType::NOOP; }

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
        std::cout << detail::getValName(instruction);
    }
#endif
};

struct VRAssume : public VROp {
    const llvm::Value *value;
    bool istrue;

    VRAssume(const llvm::Value *V, bool istrue)
    : VROp(VROpType::ASSUME), value(V), istrue(istrue) {}

    bool isTrue() const { return istrue; }
    bool isFalse() const { return !istrue; }
    const llvm::Value *getValue() const { return value; }

    static VRAssume *get(VROp *op) {
        return op->isAssume() ? static_cast<VRAssume *>(op) : nullptr;
    }

#ifndef NDEBUG
    void dump() const override {
        if (isFalse())
            std::cout << "!";
        std::cout << "[";
        std::cout << detail::getValName(value);
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

template <typename T>
class EqualityMap {
    struct _Cmp {
        bool operator()(const llvm::Value *a, const llvm::Value *b) const {
            // XXX: merge constants?
            return a < b;
        }
    };

    using SetT = std::set<T, _Cmp>;
    using ClassT = std::shared_ptr<SetT>;
    std::map<T, ClassT, _Cmp> _map;

    // FIXME: use variadic templates
    ClassT newClass(const T& a, const T& b) {
        auto cls = ClassT(new SetT());
        cls->insert(a);
        cls->insert(b);
        return cls;
    }

    ClassT newClass(const T& a) {
        auto cls = ClassT(new SetT());
        cls->insert(a);
        return cls;
    }

public:
    bool add(const T& a, const T& b) {
        auto itA = _map.find(a);
        auto itB = _map.find(b);
        if (itA == _map.end()) {
            if (itB == _map.end()) {
                if (a == b) {
                    auto newcls = newClass(a);
                    _map[a] = newcls;
                    assert(newcls.use_count() == 2);
                } else {
                    auto newcls = newClass(a, b);
                    _map[b] = newcls;
                    _map[a] = newcls;
                    assert(newcls.use_count() == 3);
                }
            } else {
                auto B = itB->second;
                B->insert(a);
                _map[a] = B;
            }
        } else {
            auto A = itA->second;
            if (itB == _map.end()) {
                A->insert(b);
                _map[b] = A;
            } else {
                // merge of classes
                auto B = itB->second;
                if (A == B)
                    return false;

                for (auto& val : *B.get()) {
                    A->insert(val);
                    _map[val] = A;
                }
                assert(B.use_count() == 1);
                A->insert(b);
                assert(_map[b] == A);
            }
        }

        assert(!_map.empty());
        assert(get(a) != nullptr);
        assert(get(a) == get(b));
        assert(get(a)->count(a) > 0);
        assert(get(a)->count(b) > 0);
        return true;
    }

    bool merge(const EqualityMap& rhs) {
        bool changed = false;
        // FIXME: not very efficient
        for (auto& it : rhs._map) {
            for (auto& eq : *it.second.get()) {
                changed |= add(it.first, eq);
            }
        }

        return changed;
    }

    SetT *get(const T& a) {
        auto it = _map.find(a);
        if (it == _map.end()) {
            return nullptr;
        }
        return it->second.get();
    }

#ifndef NDEBUG
    void dump() const {
        std::set<SetT*> classes;
        for (const auto& it : _map) {
            classes.insert(it.second.get());
        }

        if (classes.empty()) {
            return;
        }

        for (const auto cls : classes) {
            std::cout << "{";
            int t = 0;
            for (const auto& val : *cls) {
                if (++t > 1)
                    std::cout << " = ";
                std::cout << detail::getValName(val);
            }
            std::cout << "} ";
        }
        std::cout << std::endl;
    }
#endif
};

class ReadsMap {
    // pair (a,b) such that b = load a in the future
    std::map<const llvm::Value *, const llvm::Value *> _map;

public:
    auto begin() -> decltype(_map.begin()) { return _map.begin(); }
    auto end() -> decltype(_map.end()) { return _map.end(); }

    bool add(const llvm::Value *from, const llvm::Value *val) {
        assert(val != nullptr);
        auto it = _map.find(from);
        if (it == _map.end()) {
            _map.emplace_hint(it, from, val);
            return true;
        } else if (it->second == val) {
            return false;
        }

        // XXX: use the found iterator
        _map[from] = val;
        return true;
    }

    const llvm::Value *get(const llvm::Value *from) const {
        auto it = _map.find(from);
        if (it == _map.end())
            return nullptr;
        return it->second;
    }

    void intersect(const ReadsMap& rhs) {
        decltype(_map) tmp;
        for (auto& it : rhs._map) {
            if (get(it.first) == it.second)
                tmp.emplace(it.first, it.second);
        }

        _map.swap(tmp);
    }
#ifndef NDEBUG
    void dump() const {
        for (auto& it : _map) {
            std::cout << "L(" << detail::getValName(it.first) << ") = "
                      << detail::getValName(it.second) << "\n";
        }
    }
#endif // NDEBUG
};

struct VRLocation  {
    const unsigned id;

    // Valid equalities at this location
    EqualityMap<const llvm::Value *> equalities;
    ReadsMap reads;

    std::vector<VREdge *> predecessors{};
    std::vector<std::unique_ptr<VREdge>> successors{};

    void addEdge(std::unique_ptr<VREdge>&& edge) {
        edge->target->predecessors.push_back(edge.get());
        successors.emplace_back(std::move(edge));
    }

    static bool hasAlias(const llvm::Value *val,
                         EqualityMap<const llvm::Value *>& E) {
        auto equiv = E.get(val);
        if (!equiv)
            return false;
        for (auto alias : *equiv) {
            if (llvm::isa<llvm::AllocaInst>(alias)) {
                //llvm::errs() << *val << " has alias " << *alias << "\n";
                return true;
            }
        }
        return false;
    }

    bool loadGen(const llvm::LoadInst *LI,
                 EqualityMap<const llvm::Value*>& E,
                 VRLocation *source) {
        auto readFrom = LI->getOperand(0);
        auto readVal = source->reads.get(readFrom);
        if (!readVal) {
            // try read from aliases, we may get lucky there
            // (as we do not add all equivalent reads to the map of reads)
            // XXX: make an alias iterator
            auto equiv = source->equalities.get(LI->getOperand(0));
            if (!equiv)
                return false;
            for (auto alias : *equiv) {
                if ((readVal = source->reads.get(alias))) {
                    break;
                }
            }
            if (!readVal)
                return false;
        }

        return E.add(LI, readVal);
    }

    bool instructionGen(const llvm::Instruction *I,
                        EqualityMap<const llvm::Value*>& E,
                        ReadsMap& R, VRLocation *source) {
        using namespace llvm;
        if (auto SI = dyn_cast<StoreInst>(I)) {
            auto writtenMem = SI->getOperand(1)->stripPointerCasts();
            return R.add(writtenMem, SI->getOperand(0));
        } else if (auto LI = dyn_cast<LoadInst>(I)) {
            return loadGen(LI, E, source);
        }
        return false;
    }

    void instructionKills(const llvm::Instruction *I,
                        EqualityMap<const llvm::Value*>& E,
                        VRLocation *source,
                        std::set<const llvm::Value *>& overwritesReads,
                        bool& overwritesAll) {
        using namespace llvm;
        if (auto SI = dyn_cast<StoreInst>(I)) {
            auto writtenMem = SI->getOperand(1)->stripPointerCasts();
            if (isa<AllocaInst>(writtenMem) || hasAlias(writtenMem, E)) {
                overwritesReads.insert(writtenMem);
                // overwrite aliases
                if (auto equiv = source->equalities.get(writtenMem)) {
                    overwritesReads.insert(equiv->begin(), equiv->end());
                }
                // overwrite also reads from memory that has no
                // aliases to an alloca inst
                // (we do not know whether it may be alias or not)
                for (auto& r : source->reads) {
                    if (!hasAlias(r.first, E)) {
                        overwritesReads.insert(r.first);
                    }
                }
            } else {
                overwritesAll = true;
            }
        }
    }


    // collect information via an edge from a single predecessor
    // and store it in E and R
    bool collect(EqualityMap<const llvm::Value*>& E,
                 ReadsMap& R,
                 VREdge *edge) {
        auto source = edge->source;
        std::set<const llvm::Value *> overwritesReads;
        bool overwritesAll = false;
        bool changed = false;

        ///
        // -- gen
        if (edge->op->isAssume()) {
            // FIXME, may be equality too
        } else {
            auto I = VRInstruction::get(edge->op.get())->getInstruction();
            changed |= instructionGen(I, E, R, source);

            instructionKills(I, E, source, overwritesReads, overwritesAll);
        }

        ///
        // -- merge && kill
        changed |= equalities.merge(source->equalities);
        if (overwritesAll) { // no merge
            return changed;
        }

        for (auto& it : source->reads) {
            if (overwritesReads.count(it.first) > 0)
                continue;
            changed |= R.add(it.first, it.second);
        }

        return changed;
    }

    bool collect(VREdge *edge) {
        return collect(equalities, reads, edge);
    }

    // merge information from predecessors
    bool collect() {
        if (predecessors.size() > 1) {
            return mergePredecessors();
        } else if (predecessors.size() == 1 ){
            return collect(*predecessors.begin());
        }
        return false;
    }

    bool mergePredecessors() {
        // do we want it?
        return false;
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

class LLVMValueRelations {
    const llvm::Module *_M;

    unsigned last_node_id{0};
    // mapping from LLVM Values to relevant CFG nodes
    std::map<const llvm::Value *, VRLocation *> _loc_mapping;
    // list of our basic blocks with mapping from LLVM
    std::map<const llvm::BasicBlock *, std::unique_ptr<VRBBlock>> _blocks;

    VRLocation *getMapping(const llvm::Value *v) {
        auto it = _loc_mapping.find(v);
        return it == _loc_mapping.end() ? nullptr : it->second;
    }

    VRLocation *newLocation(const llvm::Value *v = nullptr) {
        auto loc = new VRLocation(++last_node_id);
        if (v)
            _loc_mapping[v] = loc;
        return loc;
    }

    VRBBlock *newBBlock(const llvm::BasicBlock *B) {
        assert(_blocks.find(B) == _blocks.end());
        auto block = new VRBBlock();
        _blocks.emplace(std::piecewise_construct,
                        std::forward_as_tuple(B),
                        std::forward_as_tuple(block));
        return block;
    }

    VRBBlock *getBBlock(const llvm::BasicBlock *B) {
        auto it = _blocks.find(B);
        return it == _blocks.end() ? nullptr : it->second.get();
    }

    void build(const llvm::BasicBlock& B) {
        auto block = newBBlock(&B);
        const llvm::Instruction *lastInst{nullptr};

        for (const auto& I : B) {
            auto loc = std::unique_ptr<VRLocation>(newLocation(&I));

            if (lastInst) {
                auto edge
                    = std::unique_ptr<VREdge>(
                            new VREdge(block->last(), loc.get(),
                                       std::unique_ptr<VROp>(new VRInstruction(lastInst))));

                block->last()->addEdge(std::move(edge));
            }

            block->append(std::move(loc));
            lastInst = &I;
        }
    }

    void build(const llvm::Function& F) {
        for (const auto& B : F) {
            assert(B.size() != 0);
            build(B);
        }

        for (const auto& B : F) {
            assert(B.size() != 0);
            auto block = getBBlock(&B);
            assert(block);

            // add generated constrains
            auto term = B.getTerminator();
            auto br = llvm::dyn_cast<llvm::BranchInst>(term);
            if (!br) {
                if (llvm::succ_begin(&B) != llvm::succ_end(&B)) {
                    llvm::errs() << "Unhandled terminator: " << *term << "\n";
                    abort();
                }
                continue; // no successor
            }

            if (br->isConditional()) {
                auto trueSucc = getBBlock(br->getSuccessor(0));
                auto falseSucc = getBBlock(br->getSuccessor(1));
                assert(trueSucc && falseSucc);
                auto trueOp
                    = std::unique_ptr<VROp>(new VRAssume(br->getCondition(), true));
                auto falseOp
                    = std::unique_ptr<VROp>(new VRAssume(br->getCondition(), false));

                auto trueEdge = std::unique_ptr<VREdge>(new VREdge(block->last(),
                                                                   trueSucc->first(),
                                                                   std::move(trueOp)));
                auto falseEdge = std::unique_ptr<VREdge>(new VREdge(block->last(),
                                                                   falseSucc->first(),
                                                                   std::move(falseOp)));
                block->last()->addEdge(std::move(trueEdge));
                block->last()->addEdge(std::move(falseEdge));
                continue;
            } else {
                auto llvmsucc = B.getSingleSuccessor();
                assert(llvmsucc);
                auto succ = getBBlock(llvmsucc);
                assert(succ);
                auto op = std::unique_ptr<VROp>(new VRNoop());
                block->last()->addEdge(std::unique_ptr<VREdge>(
                                        new VREdge(block->last(),
                                                   succ->first(),
                                                   std::move(op))));
            }
        }
    }

public:
    LLVMValueRelations(const llvm::Module *M) : _M(M) {}

    void build() {
        for (const auto& F : *_M) {
            build(F);
        }
    }

    // FIXME: this should be for each node
    void compute() {
        // FIXME: only nodes reachable from changed nodes
        bool changed;
        unsigned n = 0;
        do {
            ++n;
            changed = false;
            for (const auto& B : _blocks) {
                for (const auto& loc : B.second->locations) {
                    changed |= loc->collect();
                }
            }
        } while (changed);
        llvm::errs() << "Number of iterations: " << n << "\n";
    }

    decltype(_blocks) const& getBlocks() const {
        return _blocks;
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_VALUE_RELATION_ANALYSIS_H_
