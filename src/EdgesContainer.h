/// XXX add licence
//

#ifndef _EDGES_CONTAINER_H_
#define _EDGES_CONTAINER_H_

#include <set>
#include <cassert>

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

    size_type size() const { return container.size(); }

    bool insert(NodePtrT n)
    {
        return container.insert(n).second;
    }

    size_t erase(NodePtrT n)
    {
        return container.erase(n);
    }

    void clear()
    {
        container.clear();
    }
    
private:
    ContainerT container;
};

} // namespace dg

#endif // _BBLOCK_H_
