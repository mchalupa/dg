#ifndef DG_MEMORY_STATE_H_
#define DG_MEMORY_STATE_H_

#include "dg/util/cow_shared_ptr.h"
#include <map>

namespace dg {

// Representation of memory state with copy-on-write support
template <typename Key, typename Object>
class MemoryState {
  public:
    using Map = std::map<Key, cow_shared_ptr<Object>>;

    const Object *get(Key k) const {
        auto it = _memory.find(k);
        if (it == _memory.end())
            return nullptr;
        return it->second.get();
    };

    Object *getWritable(Key k) { return _memory[k].getWritable(); };

    void put(Key k, Object *o) { _memory[k].reset(o); }

    // take the memory state rhs and copy
    // entries for which this state does not have entry
    bool copyMissing(const MemoryState &rhs) {
        bool changed = false;
        for (const auto &rit : rhs._memory) {
            auto it = _memory.find(rit.first);
            if (it == _memory.end()) {
                _memory.emplace_hint(it, rit);
                changed = true;
            }
        }

        return changed;
    }

    bool merge(const MemoryState &rhs) {
        bool changed = false;
        for (const auto &rit : rhs._memory) {
            auto it = _memory.find(rit.first);
            if (it == _memory.end()) {
                _memory.emplace_hint(it, rit);
                changed = true;
            } else {
                // no update necessary
                if (it->second == rit.second)
                    continue;
                auto our = it->second.getWritable();
                changed |= our->merge(*rit.second.get());
            }
        }

        return changed;
    }

  private:
    Map _memory;

  public:
    auto begin() -> decltype(_memory.begin()) { return _memory.begin(); }
    auto end() -> decltype(_memory.end()) { return _memory.end(); }
    auto begin() const -> decltype(_memory.begin()) { return _memory.begin(); }
    auto end() const -> decltype(_memory.end()) { return _memory.end(); }

    // FIXME: add proper iterator
};

// copy-on-write container around MemoryState
template <typename Key, typename Object>
class COWMemoryState {
    cow_shared_ptr<MemoryState<Key, Object>> state;

    const Object *get(Key k) const { return state->get(k); };

    Object *getWritable(Key k) { return state.getWritable()->getWritable(k); };

    void put(Key k, Object *o) { state.getWritable()->put(k, o); }

    // take the memory state rhs and copy
    // entries for which this state does not have entry
    bool copyMissing(const MemoryState &rhs) {
        return state.getWritable()->copyMissing(rhs);
    }

    bool copyMissing(const COWMemoryState &rhs) {
        return state.getWritable()->copyMissing(rhs.state);
    }

    bool merge(const MemoryState &rhs) {
        return state.getWritable()->merge(rhs);
    }

    bool merge(const COWMemoryState &rhs) {
        return state.getWritable()->merge(rhs.state);
    }
};

} // namespace dg

#endif
