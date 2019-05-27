#ifndef _DG_POINTS_TO_SET_H_
#define _DG_POINTS_TO_SET_H_

#include "dg/analysis/PointsTo/PointsToSets/OffsetsSetPointsToSet.h"
#include "dg/analysis/PointsTo/PointsToSets/SimplePointsToSet.h"
#include "dg/analysis/PointsTo/PointsToSets/SeparateOffsetsPointsToSet.h"
#include "dg/analysis/PointsTo/PointsToSets/PointerIdPointsToSet.h"
#include "dg/analysis/PointsTo/PointsToSets/SmallOffsetsPointsToSet.h"
#include "dg/analysis/PointsTo/PointsToSets/AlignedSmallOffsetsPointsToSet.h"
#include "dg/analysis/PointsTo/PointsToSets/AlignedPointerIdPointsToSet.h"

namespace dg {
namespace analysis {
namespace pta {

using PointsToSetT = OffsetsSetPointsToSet;
using PointsToMapT = std::map<Offset, PointsToSetT>;

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_POINTS_TO_SET_H_
