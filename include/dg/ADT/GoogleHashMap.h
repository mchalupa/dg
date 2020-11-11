#ifndef DG_GOOGLE_SPARSEHASH_H_
#define DG_GOOGLE_SPARSEHASH_H_

#include <sparsehash/sparse_hash_map>
#include "HashMapImpl.h"

namespace dg {

template <typename Key, typename Val>
class SparseHashMap : public HashMapImpl<Key, Val, google::sparse_hash_map<Key, Val>> {
};

} // namespace dg

#endif
