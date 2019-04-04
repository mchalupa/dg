#ifndef _DG_POINTS_TO_SET_H_
#define _DG_POINTS_TO_SET_H_

#include "dg/analysis/PointsTo/OriginalPointsToSet.h"
#include "dg/analysis/PointsTo/SimplePointsToSet.h"
#include "dg/analysis/PointsTo/SeparateOffsetsPointsToSet.h"
#include "dg/analysis/PointsTo/SingleBitvectorPointsToSet.h"
#include "dg/analysis/PointsTo/SmallOffsetsPointsToSet.h"
#include "dg/analysis/PointsTo/AlignedOffsetsPointsToSet.h"
#include "dg/analysis/PointsTo/AlignedBitvectorPointsToSet.h"

namespace dg {
namespace analysis {
namespace pta {

using PointsToSetT = PointsToSet;
using PointsToMapT = std::map<Offset, PointsToSetT>;

} // namespace pta
} // namespace analysis
} // namespace dg

#endif // _DG_POINTS_TO_SET_H_
