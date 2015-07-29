#ifndef _DG_ADT_QUEUE_H_
#define _DG_ADT_QUEUE_H_

#include <stack>
#include <queue>

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

    void push(const ValueT& what)
    {
        Container.push(what);
    }

    bool empty() const
    {
        return Container.empty();
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

    void push(const ValueT& what)
    {
        Container.push(what);
    }

    bool empty() const
    {
        return Container.empty();
    }

private:
    std::queue<ValueT> Container;
};

} // namespace ADT
} // namespace dg

#endif // _DG_ADT_QUEUE_H_
