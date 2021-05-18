#ifndef DG_MAP_IMPL_H_
#define DG_MAP_IMPL_H_

#include <cstddef>
#include <map>

namespace dg {

// an ordered map with a unified API
template <typename Key, typename Val, typename Impl>
class MapImpl : public Impl {
  public:
    using iterator = typename Impl::iterator;
    using const_iterator = typename Impl::const_iterator;

    bool put(const Key &k, Val v) {
        auto it = this->insert(std::make_pair(k, v));
        return it.second;
    }

    const Val *get(const Key &k) const {
        auto it = this->find(k);
        if (it != this->end())
            return &it->second;
        return nullptr;
    }

    Val *get(Key &k) {
        auto it = this->find(k);
        if (it != this->end())
            return &it->second;
        return nullptr;
    }

    void reserve(size_t /*unused*/) {
        // so that we can exchangabily use with HashMap
    }
};

template <typename Key, typename Val>
using Map = MapImpl<Key, Val, std::map<Key, Val>>;

} // namespace dg

#endif
