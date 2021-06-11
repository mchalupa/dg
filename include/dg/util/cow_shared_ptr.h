#ifndef COW_SHARED_PTR_H_
#define COW_SHARED_PTR_H_

#include <cassert>
#include <memory>

///
// Shared pointer with copy-on-write support
template <typename T>
class cow_shared_ptr : public std::shared_ptr<T> {
    // am I the owner of the copy?
    bool owner{true};

  public:
    cow_shared_ptr() = default;
    cow_shared_ptr(T *p) : std::shared_ptr<T>(p) {}
    cow_shared_ptr(cow_shared_ptr &&) = delete;
    cow_shared_ptr(const cow_shared_ptr &rhs)
            : std::shared_ptr<T>(rhs), owner(false) {}

    void reset(T *p) {
        owner = true;
        std::shared_ptr<T>::reset(p);
    }

    const T *get() const { return std::shared_ptr<T>::get(); }
    const T *operator->() const { return get(); }
    const T *operator*() const { return get(); }

    T *getWritable() {
        if (owner)
            return std::shared_ptr<T>::get();

        // create a copy of the object and claim the ownership
        assert(!owner);
        if (get() != nullptr) {
            reset(new T(*get()));
        } else {
            reset(new T());
        }
        assert(owner); // set in reset() method
        return std::shared_ptr<T>::get();
    }
};

/*
template <typename T>
class cow_shared_ptr {
    std::shared_ptr<T> ptr{nullptr};
    // am I the owner of the copy?
    bool owner{false};

    public:
    cow_shared_ptr() = default;
    cow_shared_ptr(T *p) : ptr(p), owner(true) {}
    cow_shared_ptr(cow_shared_ptr&&) = delete;
    cow_shared_ptr(const cow_shared_ptr& rhs)
        : ptr(rhs.ptr), owner(false) {}

    const T *get() const { return ptr.get(); }

    T *getWritable() {
        if (owner)
            return ptr.get();
        // create a copy of the object and claim the ownership
        if (ptr) {
            ptr.reset(new T(*get()));
        } else {
            ptr.reset(new T());
        }
        owner = true;
        return ptr.get();
    }

    auto use_count() const -> decltype(ptr.use_count()) {
        return ptr.use_count();
    }
};
*/

#endif // COW_SHARED_PTR_H_
