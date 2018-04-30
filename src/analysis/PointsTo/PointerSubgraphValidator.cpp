#include "PointerSubgraphValidator.h"

namespace dg {
namespace analysis {
namespace pta {
namespace debug {


bool PointerSubgraphValidator::reportInvalNumberOfOperands(const PSNode *nd) {
    errors += "Invalid number of operands for " + std::string(PSNodeTypeToCString(nd->getType())) +
              " with ID " + std::to_string(nd->getID()) + "\n  - operands: [";
    for (unsigned i = 0, e =  nd->getOperandsNum(); i < e; ++i) {
        errors += std::to_string(nd->getID());
        if (i != e - 1)
            errors += " ";
    }
    errors += "]\n";

    return true;
}

bool PointerSubgraphValidator::checkOperands() {
    bool invalid = false;

    for (const PSNode *nd : PS->getNodes()) {
        // this is the first node
        // XXX: do we know this?
        if (!nd)
            continue;

        switch (nd->getType()) {
            case PSNodeType::PHI:
                if (nd->getOperandsNum() == 0) {
                    invalid |= reportInvalNumberOfOperands(nd);
                }
                break;
            case PSNodeType::NULL_ADDR:
            case PSNodeType::UNKNOWN_MEM:
            case PSNodeType::NOOP:
            case PSNodeType::FUNCTION:
            case PSNodeType::CONSTANT:
                if (nd->getOperandsNum() != 0) {
                    invalid |= reportInvalNumberOfOperands(nd);
                }
                break;
            case PSNodeType::GEP:
            case PSNodeType::LOAD:
            case PSNodeType::CAST:
            case PSNodeType::INVALIDATE_OBJECT:
            case PSNodeType::FREE:
                if (nd->getOperandsNum() != 1) {
                    invalid |= reportInvalNumberOfOperands(nd);
                }
                break;
            case PSNodeType::STORE:
            case PSNodeType::MEMCPY:
                if (nd->getOperandsNum() != 2) {
                    invalid |= reportInvalNumberOfOperands(nd);
                }
                break;
        }
    }

    return invalid;
}

bool PointerSubgraphValidator::validate() {
    bool invalid = false;

    invalid |= checkOperands();

    return invalid;
}


} // namespace debug
} // namespace pta
} // namespace analysis
} // namespace dg

