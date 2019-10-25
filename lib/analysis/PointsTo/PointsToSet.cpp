#include "dg/analysis/PointsTo/PSNode.h"
#include "dg/analysis/PointsTo/Pointer.h"

#include "dg/analysis/PointsTo/PointsToSet.h"

#include <vector>
#include <map>

namespace dg {
namespace pta {
    std::vector<PSNode*> SeparateOffsetsPointsToSet::idVector;
    std::vector<Pointer> PointerIdPointsToSet::idVector;
    std::vector<PSNode*> SmallOffsetsPointsToSet::idVector;
    std::vector<PSNode*> AlignedSmallOffsetsPointsToSet::idVector;
    std::vector<Pointer> AlignedPointerIdPointsToSet::idVector;
    std::map<PSNode*,size_t> SeparateOffsetsPointsToSet::ids;
    std::map<Pointer,size_t> PointerIdPointsToSet::ids;
    std::map<PSNode*,size_t> SmallOffsetsPointsToSet::ids;
    std::map<PSNode*,size_t> AlignedSmallOffsetsPointsToSet::ids;
    std::map<Pointer,size_t> AlignedPointerIdPointsToSet::ids;
} // namespace pta
} // namespace debug
