#ifndef _DG_POINTER_SUBGRAPH_VALIDATOR_H_
#define _DG_POINTER_SUBGRAPH_VALIDATOR_H_

#include <string>
#include "dg/analysis/PointsTo/PointerGraph.h"

namespace dg {
namespace analysis {
namespace pta {
namespace debug {


/**
 * Take PointerGraph instance and check
 * whether it is not broken
 */
class PointerGraphValidator {
    /* These methods return true if the graph is invalid */
    bool checkEdges();
    bool checkNodes();
    bool checkOperands();

    // do not check for the connectivity of the graph
    bool no_connectivity;

protected:
    const PointerGraph *PS;

    std::string errors{};
    std::string warnings{};

    virtual bool reportInvalOperands(const PSNode *n, const std::string& user_err = "");
    virtual bool reportInvalEdges(const PSNode *n, const std::string& user_err = "");
    virtual bool reportInvalNode(const PSNode *n, const std::string& user_err = "");
    virtual bool reportUnreachableNode(const PSNode *);

    virtual bool warn(const PSNode *n, const std::string& warning);

public:
    PointerGraphValidator(const PointerGraph *ps, bool no_conn = false)
    : no_connectivity(no_conn), PS(ps) {}
    virtual ~PointerGraphValidator() = default;

    bool validate();

    const std::string& getErrors() const { return errors; }
    const std::string& getWarnings() const { return warnings; }
};

} // namespace debug
} // namespace pta
} // namespace analysis
} // namespace dg



#endif // _DG_POINTER_SUBGRAPH_VALIDATOR_H_
