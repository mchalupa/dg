#ifndef _DG_POINTER_SUBGRAPH_VALIDATOR_H_
#define _DG_POINTER_SUBGRAPH_VALIDATOR_H_

#include <string>
#include "analysis/PointsTo/PointerSubgraph.h"

namespace dg {
namespace analysis {
namespace pta {
namespace debug {


/**
 * Take PointerSubgraph instance and check
 * whether it is not broken
 */
class PointerSubgraphValidator {
    const PointerSubgraph *PS;

    /* These methods return true if the graph is invalid */
    bool checkEdges();
    bool checkNodes();
    bool checkOperands();

protected:
    std::string errors{};
    std::string warnings{};

    virtual bool reportInvalOperands(const PSNode *n, const std::string& user_err = "");
    virtual bool reportInvalEdges(const PSNode *n, const std::string& user_err = "");
    virtual bool reportInvalNode(const PSNode *n, const std::string& user_err = "");
    virtual bool reportUnreachableNode(const PSNode *);

    virtual bool warn(const PSNode *n, const std::string& warning);

public:
    PointerSubgraphValidator(const PointerSubgraph *ps) : PS(ps) {}
    virtual ~PointerSubgraphValidator() = default;

    bool validate();

    const std::string& getErrors() const { return errors; }
    const std::string& getWarnings() const { return warnings; }
};

} // namespace debug
} // namespace pta
} // namespace analysis
} // namespace dg



#endif // _DG_POINTER_SUBGRAPH_VALIDATOR_H_
