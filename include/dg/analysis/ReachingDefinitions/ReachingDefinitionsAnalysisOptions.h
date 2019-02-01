#ifndef _DG_REACHING_DEFINITIONS_ANALYSIS_OPTIONS_H_
#define _DG_REACHING_DEFINITIONS_ANALYSIS_OPTIONS_H_

#include <map>

#include "dg/analysis/Offset.h"
#include "dg/analysis/AnalysisOptions.h"

namespace dg {
namespace analysis {

struct FunctionModel {
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

    struct Defines {
        unsigned operand;
        OperandValue from, to;

        Defines(unsigned operand, OperandValue from, OperandValue to)
        : operand(operand), from(from), to(to) {}
        Defines(Defines&&) = default;
        Defines(const Defines&) = default;
        Defines& operator=(const Defines& rhs) {
            operand = rhs.operand;
            from = rhs.from;
            to = rhs.to;
            return *this;
        }
    };

    std::string name;

    void add(unsigned operand, OperandValue from, OperandValue to) {
        _defines.emplace(operand, Defines{operand, from, to});
    }
    void set(unsigned operand, OperandValue from, OperandValue to) {
        _defines.emplace(operand, Defines{operand, from, to});
    }

    void set(const Defines& def) {
        _defines.emplace(def.operand, def);
    }

    const Defines *defines(unsigned operand) const {
        auto it = _defines.find(operand);
        return it == _defines.end() ? nullptr : &it->second;
    }

private:
    std::map<unsigned, Defines> _defines;
};

struct ReachingDefinitionsAnalysisOptions : AnalysisOptions {
    // Should we perform strong update with unknown memory?
    // NOTE: not sound.
    bool strongUpdateUnknown{false};

    // Undefined functions have no side-effects
    bool undefinedArePure{false};

    // Maximal size of the reaching definitions set.
    // If this size is exceeded, the set is cropped to unknown.
    Offset maxSetSize{Offset::UNKNOWN};

    // Should we perform sparse or dense analysis?
    bool sparse{false};

    // Does the analysis track concrete bytes
    // or just objects?
    bool fieldInsensitive{false};


    ReachingDefinitionsAnalysisOptions& setStrongUpdateUnknown(bool b) {
        strongUpdateUnknown = b; return *this;
    }

    ReachingDefinitionsAnalysisOptions& setUndefinedArePure(bool b) {
        undefinedArePure = b; return *this;
    }

    ReachingDefinitionsAnalysisOptions& setMaxSetSize(Offset s) {
        maxSetSize = s; return *this;
    }

    ReachingDefinitionsAnalysisOptions& setSparse(bool b) {
        sparse = b; return *this;
    }

    ReachingDefinitionsAnalysisOptions& setFieldInsensitive(bool b) {
        fieldInsensitive = b; return *this;
    }

    std::map<const std::string, FunctionModel> functionModels;

    const FunctionModel *getFunctionModel(const std::string& name) const {
        auto it = functionModels.find(name);
        return it == functionModels.end() ? nullptr : &it->second;
    }

    void functionModelSet(const std::string& name, const FunctionModel::Defines& def) {
        auto& M = functionModels[name];
        if (M.name == "")
            M.name = name;
        M.set(def);
    }
};

} // namespace analysis
} // namespace dg

#endif // _DG_REACHING_ANALYSIS_OPTIONS_H_
