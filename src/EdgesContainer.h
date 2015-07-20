/// XXX add licence
//

#ifndef _EDGES_CONTAINER_H_
#define _EDGES_CONTAINER_H_

#include <set>
#include <cassert>
#include <algorithm>

namespace dg {


/// ------------------------------------------------------------------
// - EdgesContainer
//
//   This is basically just a wrapper for real container, so that
//   we have the container defined on one place for all edges.
/// ------------------------------------------------------------------
template <typename NodePtrT, unsigned int EXPECTED_EDGES_NUM = 8>
class EdgesContainer
{
public:
    // XXX use llvm ADTs when available, or BDDs?
    typedef typename std::set<NodePtrT> ContainerT;
    typedef typename ContainerT::iterator iterator;
    typedef typename ContainerT::const_iterator const_iterator;
    typedef typename ContainerT::size_type size_type;

    iterator begin() { return container.begin(); }
    const_iterator begin() const { return container.begin(); }
    iterator end() { return container.end(); }
    const_iterator end() const { return container.end(); }

    size_type size() const
    {
        return container.size();
    }

    bool insert(NodePtrT n)
    {
        return container.insert(n).second;
    }

    bool contains(NodePtrT n) const
    {
        return container.count(n) != 0;
    }

    size_t erase(NodePtrT n)
    {
        return container.erase(n);
    }

    void clear()
    {
        container.clear();
    }

    bool empty()
    {
        return container.empty();
    }

    void swap(EdgesContainer<NodePtrT, EXPECTED_EDGES_NUM>& oth)
    {
        container.swap(oth.container);
    }

    void intersect(const EdgesContainer<NodePtrT,
                                        EXPECTED_EDGES_NUM>& oth)
    {
        EdgesContainer<NodePtrT, EXPECTED_EDGES_NUM> tmp;

        std::set_intersection(container.begin(), container.end(),
                              oth.container.begin(),
                              oth.container.end(),
                              std::inserter(tmp.container,
                                            tmp.container.begin()));

        // swap containers
        container.swap(tmp.container);
    }

    bool operator==(const EdgesContainer<NodePtrT,
                                        EXPECTED_EDGES_NUM>& oth) const
    {
        if (container.size() != oth.size())
            return false;

        // the sets are ordered, so this will work
        iterator snd = oth.container.begin();
        for (iterator fst = container.begin(), efst = container.end();
             fst != efst; ++fst, ++snd)
            if (*fst != *snd)
                return false;

        return true;
    }

    bool operator!=(const EdgesContainer<NodePtrT,
                                        EXPECTED_EDGES_NUM>& oth) const
    {
        return !operator==(oth);
    }

private:
    ContainerT container;
};

} // namespace dg

#endif // _BBLOCK_H_
