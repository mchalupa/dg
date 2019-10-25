#ifndef DG_DATA_DEPENDENCE_ANALYSIS_OPTIONS_H_
#define DG_DATA_DEPENDENCE_ANALYSIS_OPTIONS_H_

#include <map>

#include "dg/Offset.h"
#include "dg/AnalysisOptions.h"

namespace dg {

struct FunctionModel {
    std::string name;

    struct OperandValue {
        enum class Type {
            OFFSET, OPERAND
        } type{Type::OFFSET};

        union {
            Offset offset;
            unsigned operand;
        } value{0};

        bool isOffset() const { return type == Type::OFFSET; }
        bool isOperand() const { return type == Type::OPERAND; }
        Offset getOffset() const { assert(isOffset()); return value.offset; }
        unsigned getOperand() const { assert(isOperand()); return value.operand; }

        OperandValue(Offset offset) : type(Type::OFFSET) { value.offset = offset; }
        OperandValue(unsigned operand) : type(Type::OPERAND) { value.operand = operand; }
        OperandValue(const OperandValue&) = default;
        OperandValue(OperandValue&&) = default;
        OperandValue& operator=(const OperandValue& rhs) {
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
        : operand(operand), from(from), to(to) {}
        Operand(Operand&&) = default;
        Operand(const Operand&) = default;
        Operand& operator=(const Operand& rhs) {
            operand = rhs.operand;
            from = rhs.from;
            to = rhs.to;
            return *this;
        }
    };

    void addDef(unsigned operand, OperandValue from, OperandValue to) {
        _defines.emplace(operand, Operand{operand, from, to});
    }

    void addUse(unsigned operand, OperandValue from, OperandValue to) {
        _uses.emplace(operand, Operand{operand, from, to});
    }

    void addDef(const Operand& op) { _defines.emplace(op.operand, op); }
    void addUse(const Operand& op) { _uses.emplace(op.operand, op); }

    const Operand *defines(unsigned operand) const {
        auto it = _defines.find(operand);
        return it == _defines.end() ? nullptr : &it->second;
    }

    const Operand *uses(unsigned operand) const {
        auto it = _uses.find(operand);
        return it == _uses.end() ? nullptr : &it->second;
    }

    bool handles(unsigned i) const {
        return defines(i) || uses(i);
    }

private:
    std::map<unsigned, Operand> _defines;
    std::map<unsigned, Operand> _uses;
};

struct DataDependenceAnalysisOptions : AnalysisOptions {
    // default one
    enum class AnalysisType { rd, ssa } analysisType{AnalysisType::ssa};

    bool isSSA() const { return analysisType == AnalysisType::ssa;}
    bool isReachingDefinitions() const { return analysisType == AnalysisType::rd;}

    // Should we perform strong update with unknown memory?
    // NOTE: not sound.
    bool strongUpdateUnknown{false};

    // Undefined functions have no side-effects
    bool undefinedArePure{false};

    // Maximal size of the reaching definitions set.
    // If this size is exceeded, the set is cropped to unknown.
    Offset maxSetSize{Offset::UNKNOWN};

    // Does the analysis track concrete bytes
    // or just objects?
    bool fieldInsensitive{false};

    DataDependenceAnalysisOptions& setStrongUpdateUnknown(bool b) {
        strongUpdateUnknown = b; return *this;
    }

    DataDependenceAnalysisOptions& setUndefinedArePure(bool b) {
        undefinedArePure = b; return *this;
    }

    DataDependenceAnalysisOptions& setMaxSetSize(Offset s) {
        maxSetSize = s; return *this;
    }

    DataDependenceAnalysisOptions& setFieldInsensitive(bool b) {
        fieldInsensitive = b; return *this;
    }

    std::map<const std::string, FunctionModel> functionModels;

    const FunctionModel *getFunctionModel(const std::string& name) const {
        auto it = functionModels.find(name);
        return it == functionModels.end() ? nullptr : &it->second;
    }

    void functionModelAddDef(const std::string& name, const FunctionModel::Operand& def) {
        auto& M = functionModels[name];
        if (M.name == "")
            M.name = name;
        M.addDef(def);
    }

    void functionModelAddUse(const std::string& name, const FunctionModel::Operand& def) {
        auto& M = functionModels[name];
        if (M.name == "")
            M.name = name;
        M.addUse(def);
    }
};

} // namespace dg

#endif // DG_DATA_DEPENDENCE_ANALYSIS_OPTIONS_H_
