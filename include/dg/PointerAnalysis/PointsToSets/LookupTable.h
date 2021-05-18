#ifndef DG_PTSETS_LOOKUPTABLE_H_
#define DG_PTSETS_LOOKUPTABLE_H_

#include <map>
#include <vector>

#if defined(HAVE_TSL_HOPSCOTCH) || (__clang__)
#include "dg/ADT/HashMap.h"
#else
#include "dg/ADT/Map.h"
#endif

#include "dg/Offset.h"
#include "dg/PointerAnalysis/Pointer.h"

namespace dg {

class PointerIDLookupTable {
  public:
    using IDTy = size_t;
    using Pointer = pta::Pointer;
    using PSNode = pta::PSNode;
#if defined(HAVE_TSL_HOPSCOTCH) || (__clang__)
    using PtrToIDMap = dg::HashMap<PSNode *, dg::HashMap<dg::Offset, IDTy>>;
#else
    // we create the lookup table statically and there is a bug in GCC
    // that breaks statically created std::unordered_map.
    // So if we have not Hopscotch map, use std::map instead.
    using PtrToIDMap = dg::Map<PSNode *, dg::Map<dg::Offset, IDTy>>;
#endif

    // this will get a new ID for the pointer if not present
    IDTy getOrCreate(const Pointer &ptr) {
        auto res = get(ptr);
        if (res != 0)
            return res;

        _idToPtr.push_back(ptr);
        res = _idToPtr.size();
#ifndef NDEBUG
        bool r =
#endif
                _ptrToID[ptr.target].put(ptr.offset, res);

        assert(r && "Duplicated ID!");
        assert(get(res) == ptr);
        assert(res == get(ptr));
        assert(res > 0 && "ID must always be greater than 0");
        return res;
    }

    IDTy get(const Pointer &ptr) const {
        auto it = _ptrToID.find(ptr.target);
        if (it == _ptrToID.end()) {
            return 0; // invalid ID
        }
        auto it2 = it->second.find(ptr.offset);
        if (it2 == it->second.end())
            return 0;
        return it2->second;
    }

    const Pointer &get(IDTy id) const {
        assert(id - 1 < _idToPtr.size());
        return _idToPtr[id - 1];
    }

  private:
    // PSNode -> (Offset -> id)
    // Not space efficient, but we need mainly the time efficiency here...
    // NOTE: unfortunately, atm, we cannot use the id of the target for hashing
    // because it would break repeated runs of the analysis
    // as multiple graphs will contain nodes with the same id
    // (and resetting the state is really painful, I tried that,
    // but just didn't succeed).
    PtrToIDMap _ptrToID;
    std::vector<Pointer> _idToPtr; // starts from 0 (pointer = idVector[id - 1])
};

/*
class PointerIDLookupTable {
public:
    using IDTy = size_t;
    using Pointer = pta::Pointer;

    // this will get a new ID for the pointer if not present
    IDTy getOrCreate(const Pointer& ptr) {
        auto it = _ptrToID.find(ptr);
        if (it != _ptrToID.end()) {
            return it->second;
        }
        auto res = _ptrToID.size() + 1;
        assert(_idToPtr.size() == res - 1);

        _idToPtr.push_back(ptr);
        _ptrToID.emplace(ptr, _ptrToID.size() + 1);

        assert(get(res) == ptr);
        assert(res == get(ptr));
        return res;

       //bool r = _ptrToID.put(ptr, ids.size() + 1);
       //assert(r && "Duplicated ID!");
       //return ids.size() + 1;
    }

    IDTy get(const Pointer& ptr) const {
        auto it = _ptrToID.find(ptr);
        if (it != _ptrToID.end()) {
            return it->second;
        }
        return 0; // invalid ID
    }

    const Pointer& get(IDTy id) const {
        assert(id - 1 < _idToPtr.size());
        return _idToPtr[id - 1];
    }

private:
    std::map<Pointer, IDTy> _ptrToID;
    std::vector<Pointer> _idToPtr; //starts from 0 (pointer = idVector[id - 1])
};
*/

} // namespace dg

#endif
