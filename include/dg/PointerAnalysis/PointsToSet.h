#ifndef DG_POINTS_TO_SET_H_
#define DG_POINTS_TO_SET_H_

#include "dg/PointerAnalysis/PointsToSets/AlignedPointerIdPointsToSet.h"
#include "dg/PointerAnalysis/PointsToSets/AlignedSmallOffsetsPointsToSet.h"
#include "dg/PointerAnalysis/PointsToSets/OffsetsSetPointsToSet.h"
#include "dg/PointerAnalysis/PointsToSets/PointerIdPointsToSet.h"
#include "dg/PointerAnalysis/PointsToSets/SeparateOffsetsPointsToSet.h"
#include "dg/PointerAnalysis/PointsToSets/SimplePointsToSet.h"
#include "dg/PointerAnalysis/PointsToSets/SmallOffsetsPointsToSet.h"

namespace dg {
namespace pta {

using PointsToSetT = PointerIdPointsToSet;
using PointsToMapT = std::map<Offset, PointsToSetT>;

} // namespace pta
} // namespace dg

#endif
