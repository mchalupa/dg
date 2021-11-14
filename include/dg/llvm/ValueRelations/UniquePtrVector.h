#ifndef DG_LLVM_VALUE_RELATIONS_UNIQUE_PTR_VECTOR_H_
#define DG_LLVM_VALUE_RELATIONS_UNIQUE_PTR_VECTOR_H_

#include <memory>
#include <vector>

namespace dg {
namespace vr {

template <typename T>
class UniquePtrVector {
    using Container = std::vector<std::unique_ptr<T>>;
    using size_type = typename Container::size_type;
    using value_type = T;
    using reference = value_type &;

    Container _v;

  public:
    struct iterator {
        friend class UniquePtrVector<T>;

        using ContainerIterator = typename Container::const_iterator;

        using value_type = T;
        using reference = value_type &;
        using pointer = value_type *;
        using difference_type = typename ContainerIterator::difference_type;
        using iterator_category =
                std::bidirectional_iterator_tag; // std::forward_iterator_tag;

        iterator() = default;
        iterator(ContainerIterator i) : it(i) {}

        reference operator*() const { return **it; }
        pointer operator->() const { return &operator*(); }

        friend bool operator==(const iterator &lt, const iterator &rt) {
            return lt.it == rt.it;
        }

        friend bool operator!=(const iterator &lt, const iterator &rt) {
            return !(lt == rt);
        }

        iterator &operator++() {
            ++it;
            return *this;
        }

        iterator operator++(int) {
            auto copy = *this;
            ++*this;
            return copy;
        }

        iterator &operator--() {
            --it;
            return *this;
        }

        iterator operator--(int) {
            auto copy = *this;
            --*this;
            return copy;
        }

      private:
        ContainerIterator it;
    };

    reference at(size_type pos) const { return *_v.at(pos); }
    reference operator[](size_type pos) const { return *_v[pos]; }

    reference front() const { return *_v.front(); }
    reference back() const { return *_v.back(); }

    bool empty() const { return _v.empty(); }
    size_type size() const { return _v.size(); }

    void clear() { _v.clear(); }

    iterator begin() const { return iterator(_v.begin()); }
    iterator end() const { return iterator(_v.end()); }

    template <typename TT>
    void push_back(TT &&val) {
        _v.emplace_back(new T(std::forward<TT>(val)));
    }

    template <typename... Args>
    void emplace_back(Args &&...args) {
        _v.emplace_back(new T(std::forward<Args>(args)...));
    }

    iterator erase(iterator pos) { return iterator(_v.erase(pos.it)); }

    iterator erase(iterator b, iterator e) {
        return iterator(_v.erase(b.it, e.it));
    }

    void swap(UniquePtrVector &other) {
        using std::swap;
        swap(_v, other._v);
    }

    friend void swap(UniquePtrVector &lt, UniquePtrVector &rt) { lt.swap(rt); }
};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_VALUE_RELATIONS_UNIQUE_PTR_VECTOR_H_
