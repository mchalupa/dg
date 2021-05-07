#ifndef DG_HASH_MAP_IMPL_H_
#define DG_HASH_MAP_IMPL_H_

namespace dg {

template <typename Key, typename Val, typename Impl>
class HashMapImpl : public Impl {
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
};

} // namespace dg

#endif
