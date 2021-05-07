#ifndef DG_TSL_HOPSCOTCH_MAP_H_
#define DG_TSL_HOPSCOTCH_MAP_H_

#include <tsl/hopscotch_map.h>

#include "HashMapImpl.h"

namespace dg {

template <typename Key, typename Val>
class HopscotchHashMap
        : public HashMapImpl<Key, Val, tsl::hopscotch_map<Key, Val>> {};

} // namespace dg

#endif
