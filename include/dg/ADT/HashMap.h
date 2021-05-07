#ifndef DG_HASH_MAP_H_
#define DG_HASH_MAP_H_

#ifdef HAVE_TSL_HOPSCOTCH
#include "TslHopscotchHashMap.h"
namespace dg {
template <typename Key, typename Val>
using HashMap = HopscotchHashMap<Key, Val>;
}
#else
#include "STLHashMap.h"
namespace dg {
template <typename Key, typename Val>
using HashMap = STLHashMap<Key, Val>;
}
#endif

#endif
