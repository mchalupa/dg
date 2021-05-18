#ifndef DG_ADT_QUEUE_H_
#define DG_ADT_QUEUE_H_

#include <queue>
#include <set>
#include <stack>

namespace dg {
namespace ADT {

template <typename ValueT>
class QueueLIFO {
    using ContainerT = std::stack<ValueT>;

  public:
    using ValueType = ValueT;

    ValueT pop() {
        ValueT ret = Container.top();
        Container.pop();

        return ret;
    }

    ValueT &top() { return Container.top(); }

    void push(const ValueT &what) { Container.push(what); }

    bool empty() const { return Container.empty(); }

    typename ContainerT::size_type size() const { return Container.size(); }

    void swap(QueueLIFO<ValueT> &oth) { Container.swap(oth.Container); }

  private:
    ContainerT Container;
};

template <typename ValueT>
class QueueFIFO {
    using ContainerT = std::queue<ValueT>;

  public:
    using ValueType = ValueT;

    ValueT pop() {
        ValueT ret = Container.front();
        Container.pop();

        return ret;
    }

    ValueT &top() { return Container.top(); }

    void push(const ValueT &what) { Container.push(what); }

    bool empty() const { return Container.empty(); }

    typename ContainerT::size_type size() const { return Container.size(); }

    void swap(QueueFIFO<ValueT> &oth) { Container.swap(oth.Container); }

  private:
    ContainerT Container;
};

template <typename ValueT, typename Comp>
class PrioritySet {
    using ContainerT = std::set<ValueT, Comp>;

  public:
    using ValueType = ValueT;

    ValueT pop() {
        ValueT ret = *(Container.begin());
        Container.erase(Container.begin());

        return ret;
    }

    void push(const ValueT &what) { Container.insert(what); }

    bool empty() const { return Container.empty(); }

    typename ContainerT::size_type size() const { return Container.size(); }

  private:
    ContainerT Container;
};

} // namespace ADT
} // namespace dg

#endif // DG_ADT_QUEUE_H_
