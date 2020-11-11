#ifndef DG_PTSETS_LOOKUPTABLE_H_
#define DG_PTSETS_LOOKUPTABLE_H_

#include "dg/ADT/HashMap.h"
#include <map>
#include <vector>

#include "dg/PointerAnalysis/Pointer.h"

namespace dg {

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
        bool r = _ptrToID.put(ptr, res);
        assert(r && "Duplicated ID!");

        assert(get(res) == ptr);
        assert(res == get(ptr));
        return res;
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
    dg::HashMap<Pointer, IDTy> _ptrToID;
    std::vector<Pointer> _idToPtr; //starts from 0 (pointer = idVector[id - 1])
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
