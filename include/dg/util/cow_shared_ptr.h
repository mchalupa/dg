#ifndef _COW_SHARED_PTR_H_
#define _COW_SHARED_PTR_H_

#include <shared_ptr>

///
// Shared pointer with copy-on-write support
template <typename T>
class cow_shared_ptr {
    std::shared_ptr<T> ptr{nullptr};
    // am I the owner of the copy?
    bool owner{false};

    public:
    cow_shared_ptr() = default;
    cow_shared_ptr(T *p) : ptr(p) {}
    cow_shared_ptr(cow_shared_ptr&&) = delete;
    cow_shared_ptr(const cow_shared_ptr& rhs)
        : ptr(rhs.ptr), owner(false) {}

    const T *get() const { return ptr.get(); }

    T *getWriteable() {
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
};

#endif  // _COW_SHARED_PTR_H_
