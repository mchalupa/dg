#ifndef DG_DATA_DEPENDENCE_ANALYSIS_OPTIONS_H_
#define DG_DATA_DEPENDENCE_ANALYSIS_OPTIONS_H_

#include <cassert>
#include <map>
#include <utility>

#include "dg/AnalysisOptions.h"
#include "dg/Offset.h"

namespace dg {

struct FunctionModel {
    std::string name;

    struct OperandValue {
        enum class Type { OFFSET, OPERAND } type{Type::OFFSET};

        union {
            Offset offset;
            unsigned operand;
        } value{0};

        bool isOffset() const { return type == Type::OFFSET; }
        bool isOperand() const { return type == Type::OPERAND; }
        Offset getOffset() const {
            assert(isOffset());
            return value.offset;
        }
        unsigned getOperand() const {
            assert(isOperand());
            return value.operand;
        }

        OperandValue(Offset offset) { value.offset = offset; }
        OperandValue(unsigned operand) : type(Type::OPERAND) {
            value.operand = operand;
        }
        OperandValue(const OperandValue &) = default;
        OperandValue(OperandValue &&) = default;
        OperandValue &operator=(const OperandValue &rhs) {
            type = rhs.type;
            if (rhs.isOffset())
                value.offset = rhs.value.offset;
            else
                value.operand = rhs.value.operand;
            return *this;
        }
    };

    struct Operand {
        unsigned operand;
        OperandValue from, to;

        Operand(unsigned operand, OperandValue from, OperandValue to)
                : operand(operand), from(std::move(from)), to(std::move(to)) {}
        Operand(Operand &&) = default;
        Operand(const Operand &) = default;
        Operand &operator=(const Operand &rhs) = default;
    };

    void addDef(unsigned operand, OperandValue from, OperandValue to) {
        _defines.emplace(operand, Operand{operand, from, to});
    }

    void addUse(unsigned operand, OperandValue from, OperandValue to) {
        _uses.emplace(operand, Operand{operand, from, to});
    }

    void addDef(const Operand &op) { _defines.emplace(op.operand, op); }
    void addUse(const Operand &op) { _uses.emplace(op.operand, op); }

    const Operand *defines(unsigned operand) const {
        auto it = _defines.find(operand);
        return it == _defines.end() ? nullptr : &it->second;
    }

    const Operand *uses(unsigned operand) const {
        auto it = _uses.find(operand);
        return it == _uses.end() ? nullptr : &it->second;
    }

    bool handles(unsigned i) const { return defines(i) || uses(i); }

  private:
    std::map<unsigned, Operand> _defines;
    std::map<unsigned, Operand> _uses;
};

namespace dda {
enum UndefinedFunsBehavior {
    PURE = 0,
    WRITE_ANY = 1,
    READ_ANY = 1 << 1,
    WRITE_ARGS = 1 << 2,
    READ_ARGS = 1 << 3,
};
} // namespace dda

struct DataDependenceAnalysisOptions : AnalysisOptions {
    // default one
    enum class AnalysisType { ssa } analysisType{AnalysisType::ssa};

    bool isSSA() const { return analysisType == AnalysisType::ssa; }

    dda::UndefinedFunsBehavior undefinedFunsBehavior{dda::READ_ARGS};

    // Does the analysis track concrete bytes
    // or just objects?
    bool fieldInsensitive{false};

    bool undefinedArePure() const { return undefinedFunsBehavior == dda::PURE; }
    bool undefinedFunsWriteAny() const {
        return undefinedFunsBehavior & dda::WRITE_ANY;
    }
    bool undefinedFunsReadAny() const {
        return undefinedFunsBehavior & dda::READ_ANY;
    }
    bool undefinedFunsWriteArgs() const {
        return undefinedFunsBehavior & dda::WRITE_ARGS;
    }
    bool undefinedFunsReadArgs() const {
        return undefinedFunsBehavior & dda::READ_ARGS;
    }

    DataDependenceAnalysisOptions &setFieldInsensitive(bool b) {
        fieldInsensitive = b;
        return *this;
    }

    std::map<const std::string, FunctionModel> functionModels;

    const FunctionModel *getFunctionModel(const std::string &name) const {
        auto it = functionModels.find(name);
        return it == functionModels.end() ? nullptr : &it->second;
    }

    void functionModelAddDef(const std::string &name,
                             const FunctionModel::Operand &def) {
        auto &M = functionModels[name];
        if (M.name.empty())
            M.name = name;
        M.addDef(def);
    }

    void functionModelAddUse(const std::string &name,
                             const FunctionModel::Operand &def) {
        auto &M = functionModels[name];
        if (M.name.empty())
            M.name = name;
        M.addUse(def);
    }
};

} // namespace dg

#endif // DG_DATA_DEPENDENCE_ANALYSIS_OPTIONS_H_
