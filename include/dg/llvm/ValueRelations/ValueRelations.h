#ifndef DG_LLVM_RELATIONS_MAP_H_
#define DG_LLVM_RELATIONS_MAP_H_

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <stack>
#include <tuple>
#include <vector>

#include <cassert>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Value.h>

#ifndef NDEBUG
#include "getValName.h"
#include <iostream>
#endif

#include "VR.h"

namespace dg {
namespace vr {

using ValueRelations = VR;
using Relation = Relations::Type;

} // namespace vr
} // namespace dg

#endif // DG_LLVM_RELATIONS_MAP_H_
