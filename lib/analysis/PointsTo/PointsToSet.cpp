#include "dg/analysis/PointsTo/PointsToSet.h"
#include "dg/analysis/PointsTo/PSNode.h"
#include "dg/analysis/PointsTo/Pointer.h"
#include "dg/analysis/PointsTo/AlignedBitvectorPointsToSet.h"

#include <vector>
#include <map>

namespace dg {
namespace analysis {
namespace pta {
    std::vector<PSNode*> SeparateOffsetsPointsToSet::idVector;
    std::vector<Pointer> SingleBitvectorPointsToSet::idVector;
    std::vector<PSNode*> SmallOffsetsPointsToSet::idVector;
    std::vector<PSNode*> AlignedOffsetsPointsToSet::idVector;
    std::vector<Pointer> AlignedBitvectorPointsToSet::idVector;
    std::map<PSNode*,size_t> SeparateOffsetsPointsToSet::ids;
    std::map<Pointer,size_t> SingleBitvectorPointsToSet::ids;
    std::map<PSNode*,size_t> SmallOffsetsPointsToSet::ids;
    std::map<PSNode*,size_t> AlignedOffsetsPointsToSet::ids;
    std::map<Pointer,size_t> AlignedBitvectorPointsToSet::ids;
} // namespace pta
} // namespace analysis
} // namespace debug