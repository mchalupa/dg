#ifndef _DG_ADT_QUEUE_H_
#define _DG_ADT_QUEUE_H_

#include <stack>
#include <queue>
#include <set>

namespace dg {
namespace ADT {

template <typename ValueT>
class QueueLIFO
{
public:
    ValueT pop()
    {
        ValueT ret = Container.top();
        Container.pop();

        return ret;
    }

    ValueT& top()
    {
        return Container.top();
    }

    void push(const ValueT& what)
    {
        Container.push(what);
    }

    bool empty() const
    {
        return Container.empty();
    }

    size_t size() const
    {
        return Container.size();
    }

    void swap(QueueLIFO<ValueT>& oth)
    {
        Container.swap(oth.Container);
    }

private:
    std::stack<ValueT> Container;
};

template <typename ValueT>
class QueueFIFO
{
public:
    ValueT pop()
    {
        ValueT ret = Container.front();
        Container.pop();

        return ret;
    }

    ValueT& top()
    {
        return Container.top();
    }

    void push(const ValueT& what)
    {
        Container.push(what);
    }

    bool empty() const
    {
        return Container.empty();
    }

    size_t size() const
    {
        return Container.size();
    }

    void swap(QueueFIFO<ValueT>& oth)
    {
        Container.swap(oth.Container);
    }

private:
    std::queue<ValueT> Container;
};

template <typename ValueT, typename Comp>
class PrioritySet
{
public:
    ValueT pop()
    {
        ValueT ret = *(Container.begin());
        Container.erase(Container.begin());

        return ret;
    }

    void push(const ValueT& what)
    {
        Container.insert(what);
    }

    bool empty() const
    {
        return Container.empty();
    }

    size_t size() const
    {
        return Container.size();
    }

private:
    std::set<ValueT, Comp> Container;
};

} // namespace ADT
} // namespace dg

#endif // _DG_ADT_QUEUE_H_
