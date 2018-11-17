#ifndef _DG_LLVM_READS_MAP_H_
#define _DG_LLVM_READS_MAP_H_

#include <memory>
#include <cassert>

#ifndef NDEBUG
#include "getValName.h"
#endif

namespace llvm {
    class Value;
}

namespace dg {

class ReadsMap {
    // pair (a,b) such that b = load a in the future
    std::map<const llvm::Value *, const llvm::Value *> _map;

public:
    auto begin() -> decltype(_map.begin()) { return _map.begin(); }
    auto end() -> decltype(_map.end()) { return _map.end(); }
    auto begin() const -> decltype(_map.begin()) { return _map.begin(); }
    auto end() const -> decltype(_map.end()) { return _map.end(); }

    bool add(const llvm::Value *from, const llvm::Value *val) {
        assert(val != nullptr);
        auto it = _map.find(from);
        if (it == _map.end()) {
            _map.emplace_hint(it, from, val);
            return true;
        } else if (it->second == val) {
            return false;
        }

        // XXX: use the found iterator
        _map[from] = val;
        return true;
    }

	bool add(const ReadsMap& rhs) {
		bool changed = false;
		for (const auto& it : rhs) {
			assert(get(it.first) == nullptr || get(it.first) == it.second);
			changed |= add(it.first, it.second);
		}
		return changed;
	}

    const llvm::Value *get(const llvm::Value *from) const {
        auto it = _map.find(from);
        if (it == _map.end())
            return nullptr;
        return it->second;
    }

    void intersect(const ReadsMap& rhs) {
        decltype(_map) tmp;
        for (auto& it : rhs._map) {
            if (get(it.first) == it.second)
                tmp.emplace(it.first, it.second);
        }

        _map.swap(tmp);
    }

#ifndef NDEBUG
    void dump() const {
        for (auto& it : _map) {
            std::cout << "L(" << debug::getValName(it.first) << ") = "
                      << debug::getValName(it.second) << "\n";
        }
    }
#endif // NDEBUG
};

} // namespace dg

#endif // _DG_LLVM_READS_MAP_H_
