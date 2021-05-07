#include <functional>
#include <map>
#include <vector>

#include "dg/PointerAnalysis/PSNode.h"
#include "dg/PointerAnalysis/Pointer.h"
#include "dg/PointerAnalysis/PointsToSet.h"
#include "dg/PointerAnalysis/PointsToSets/LookupTable.h"

namespace dg {
namespace pta {

std::vector<PSNode *> SeparateOffsetsPointsToSet::idVector;
std::vector<PSNode *> SmallOffsetsPointsToSet::idVector;
std::vector<PSNode *> AlignedSmallOffsetsPointsToSet::idVector;
std::vector<Pointer> AlignedPointerIdPointsToSet::idVector;
std::map<PSNode *, size_t> SeparateOffsetsPointsToSet::ids;
dg::PointerIDLookupTable PointerIdPointsToSet::lookupTable;
std::map<PSNode *, size_t> SmallOffsetsPointsToSet::ids;
std::map<PSNode *, size_t> AlignedSmallOffsetsPointsToSet::ids;
std::map<Pointer, size_t> AlignedPointerIdPointsToSet::ids;

} // namespace pta

} // namespace dg
