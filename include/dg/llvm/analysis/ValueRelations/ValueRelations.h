#ifndef _DG_LLVM_VALUE_RELATION_ANALYSIS_H_
#define _DG_LLVM_VALUE_RELATION_ANALYSIS_H_

#include <list>

#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/CFG.h>

#include "dg/analysis/ValueRelations/ValueRelations.h"

namespace dg {
namespace analysis {

struct VRLocation;
struct VREdge {
    VRLocation *source;
    VRLocation *target;

    VRInfo info{};

    VREdge(VRLocation *s, VRLocation *t) : source(s), target(t) {}
};

template <typename T>
class EqualityMap {
    struct _Cmp {
        bool operator()(const VRValue *a, const VRValue *b) const {
            return *a < *b;
        }
    };

    using SetT = std::set<T, _Cmp>;
    using ClassT = std::shared_ptr<SetT>;
    std::map<T, ClassT, _Cmp> _map;

    ClassT newClass(const T& a, const T& b) {
        auto cls = ClassT(new SetT());
        cls->insert(a);
        cls->insert(b);
        return cls;
    }

public:
    void add(const T& a, const T& b) {
        auto itA = _map.find(a);
        auto itB = _map.find(b);
        if (itA == _map.end()) {
            if (itB == _map.end()) {
                auto newcls = newClass(a, b);
                _map[b] = newcls;
                _map[a] = newcls;
                assert(newcls.use_count() == 3);
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
                    return;

                for (auto& val : *B.get()) {
                    A->insert(val);
                    _map[val] = A;
                }
                assert(B.use_count() == 1);
                A->insert(b);
                assert(_map[b] == A);
            }
        }
    }

#ifndef NDEBUG
    void dump() const {
        std::set<SetT*> classes;
        for (const auto& it : _map) {
            classes.insert(it.second.get());
        }

        if (classes.empty())
            return;

        for (const auto cls : classes) {
            std::cout << "{"; 
            int t = 0;
            for (const auto& val : *cls) {
                if (++t > 1)
                    std::cout << " = ";
                val->dump();
            }
            std::cout << "} "; 
        }
        std::cout << std::endl;
    }
#endif
};

struct VRLocation  {
    const unsigned id;
    // for debugging
    const llvm::Value *val{nullptr};

    // Valid equalities at this location
    EqualityMap<VRValue *> equalities;

    // relations valid at this point
    VRRelations relations;

    std::vector<VREdge *> predecessors{};
    std::vector<std::unique_ptr<VREdge>> successors{};

    void addEdge(std::unique_ptr<VREdge>&& edge) {
        edge->target->predecessors.push_back(edge.get());
        successors.emplace_back(std::move(edge));
    }

    bool add(const VRRelations& rhs) {
        bool changed;
        for (const auto& rel : rhs.eqRelations) {
            if (relations.add(rel)) {
                equalities.add(rel.getLHS(), rel.getRHS());
                changed = true;
            }
        }
        for (const auto& rel : rhs.relations) {
            changed |= relations.add(rel);
        }
        return changed;
    }

    VRLocation(unsigned _id,
               const llvm::Value *v = nullptr)
    : id(_id), val(v) {}

    VRRelations collect(const VREdge *edge) {
        VRLocation *source = edge->source;
        VRRelations result = source->relations;
        // XXX: forgets
        // XXX: do it efficiently using iterators
        result.add(edge->info.generates());
        return result;
    }

    VRRelations collect() {
        auto it = predecessors.begin();

        VRRelations result = collect(*it);
        while (it != predecessors.end()) {
            result.intersect(collect(*it));
        }

        return result;
    }

    bool mergeIncoming() {
        bool changed = false;
        relations = collect();
        return false;
        //return changed;
    }

#ifndef NDEBUG
    void dump() const {
        std::cout << id << " "; 
        relations.dump();
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
    unsigned last_value_id{0};
    using MappingT = std::map<const llvm::Value *, std::unique_ptr<VRValue>>;

    MappingT _values; // created values
    std::vector<std::unique_ptr<VRConstant>> _constants;
    std::vector<std::unique_ptr<VRRead>> _reads;

    unsigned last_node_id{0};
    // mapping from LLVM Values to relevant CFG nodes
    std::map<const llvm::Value *, VRLocation *> _llvm_mapping;
    // list of our basic blocks with mapping from LLVM
    std::map<const llvm::BasicBlock *, std::unique_ptr<VRBBlock>> _blocks;

    VRValue *newVariable(const llvm::Value *v) {
        auto it = _values.emplace(std::piecewise_construct,
                                  std::forward_as_tuple(v),
                                  std::forward_as_tuple(new VRVariable(++last_value_id)));
        return it.first->second.get();
    }

    VRValue *getVariable(const llvm::Value *val) {
        auto it = _values.find(val);
        if (it == _values.end())
            return nullptr;

        return it->second.get();
    }

    VRRead *newRead(VRValue *from) {
        _reads.emplace_back(new VRRead(from));
        return _reads.back().get();;
    }

    VRValue *getConstant(const llvm::Value *v) {
        if (auto C = llvm::dyn_cast<llvm::ConstantInt>(v)) {
            _constants.emplace_back(new VRConstant(C->getZExtValue()));
            return _constants.back().get();;
        }
        return nullptr;
    }

    VRLocation *getMapping(const llvm::Value *v) {
        auto it = _llvm_mapping.find(v);
        return it == _llvm_mapping.end() ? nullptr : it->second;
    }

    VRValue *getOperand(const llvm::Value *val) {
        if (auto C = getConstant(val))
            return C;
        return getVariable(val);
    }

    VRLocation *newLocation(const llvm::Value *v = nullptr) {
        auto loc = new VRLocation(++last_node_id,  v);
        if (v)
            _llvm_mapping[v] = loc;
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


    bool getRelations(const llvm::Instruction& I,
                      VRInfo& result) {
        using namespace llvm;

        if (auto AI = dyn_cast<AllocaInst>(&I)) {
            newVariable(AI);
            return false;
        } else if (auto SI = dyn_cast<StoreInst>(&I)) {
            auto val = getOperand(SI->getOperand(0));
            auto var = getOperand(SI->getOperand(1));
            // we can duplicate reads here... but who cares?
            if (!val || !var)
                return {};
            auto read = newRead(var);
            result.addGen(VREq(val, newRead(var)));
            result.addForget(read);
            return true;
        } else if (auto LI = dyn_cast<LoadInst>(&I)) {
            if (auto var = getOperand(LI->getOperand(0))) {
                result.addGen(VREq(newVariable(LI), newRead(var)));
                return true;
            }
        } else if (isa<CallInst>(&I)) {
                result.addForgetAll();
                return true;
        }

        return false;
    }

    bool getBranches(const llvm::BranchInst *BI,
                     VRRelation& trueRel, VRRelation& falseRel) {
        using namespace llvm;

        if (BI->isUnconditional())
            return false;

        auto Cond = BI->getCondition();
        auto CMP = dyn_cast<ICmpInst>(Cond);
        if (!CMP)
            return false;

        auto val1 = getOperand(CMP->getOperand(0));
        auto val2 = getOperand(CMP->getOperand(1));
        // we can duplicate reads here... but who cares?
        if (!val1 || !val2)
            return false;

        switch (CMP->getSignedPredicate()) {
            case ICmpInst::Predicate::ICMP_EQ:
                trueRel = VREq(val1, val2);
                falseRel = VRNeq(val1, val2);
                return true;
            case ICmpInst::Predicate::ICMP_ULE:
            case ICmpInst::Predicate::ICMP_SLE:
                trueRel = VRLe(val1, val2);
                falseRel = VRGt(val1, val2);
                return true;
            case ICmpInst::Predicate::ICMP_UGE:
            case ICmpInst::Predicate::ICMP_SGE:
                trueRel = VRGe(val1, val2);
                falseRel = VRLt(val1, val2);
                return true;
            case ICmpInst::Predicate::ICMP_UGT:
            case ICmpInst::Predicate::ICMP_SGT:
                trueRel = VRGt(val1, val2);
                falseRel = VRLe(val1, val2);
                return true;;
            case ICmpInst::Predicate::ICMP_ULT:
            case ICmpInst::Predicate::ICMP_SLT:
                trueRel = VRLt(val1, val2);
                falseRel = VRGe(val1, val2);
                return true;
            default: abort();
        }

        return false;
    }

    void build(const llvm::BasicBlock& B) {
        auto block = newBBlock(&B);
        const llvm::Instruction *lastInst{nullptr};

        for (const auto& I : B) {
            auto loc = std::unique_ptr<VRLocation>(newLocation(&I));

            if (lastInst) {
                auto edge = std::unique_ptr<VREdge>(
                                new VREdge(block->last(), loc.get()));

                // add edge from the immediate predecessor
                VRInfo info;
                if (getRelations(*lastInst, info)) {
                    edge->info.add(info);
                }

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
            VRRelation trueRel, falseRel;
            bool hasRelations = br && getBranches(br, trueRel, falseRel);
            if (hasRelations) {
                auto trueSucc = getBBlock(br->getSuccessor(0));
                auto falseSucc = getBBlock(br->getSuccessor(1));
                assert(trueSucc && falseSucc);
                auto trueEdge = std::unique_ptr<VREdge>(new VREdge(block->last(),
                                                                   trueSucc->first()));
                auto falseEdge = std::unique_ptr<VREdge>(new VREdge(block->last(),
                                                                   falseSucc->first()));
                trueEdge->info.addGen(trueRel);
                falseEdge->info.addGen(falseRel);
                block->last()->addEdge(std::move(trueEdge));
                block->last()->addEdge(std::move(falseEdge));
                continue;
            } else {
                for (auto it = llvm::succ_begin(&B), et = llvm::succ_end(&B);
                     it != et; ++it) {
                    auto succ = getBBlock(*it);
                    assert(succ);
                    block->last()->addEdge(std::unique_ptr<VREdge>(
                                            new VREdge(block->last(),
                                                       succ->first())));
                }
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
        bool changed = false;
        unsigned n = 0;
        do {
            llvm::errs() << "Iteration " << ++n << "\n";
            for (const auto& B : _blocks) {
                for (const auto& loc : B.second->locations) {
                    changed |= loc->mergeIncoming();
                }
            }
        } while (changed);
    }

    decltype(_blocks) const& getBlocks() const {
        return _blocks;
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_LLVM_VALUE_RELATION_ANALYSIS_H_
