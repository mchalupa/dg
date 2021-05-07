#ifndef DG_ADT_SET_QUEUE_H_
#define DG_ADT_SET_QUEUE_H_

#include "Queue.h"
#include <set>

namespace dg {
namespace ADT {

// A queue where each element can be queued only once
template <typename QueueT>
class SetQueue {
    std::set<typename QueueT::ValueType> _queued;
    QueueT _queue;

  public:
    using ValueType = typename QueueT::ValueType;

    ValueType pop() { return _queue.pop(); }
    ValueType &top() { return _queue.top(); }
    bool empty() const { return _queue.empty(); }
    size_t size() const { return _queue.size(); }

    void push(const ValueType &what) {
        if (_queued.insert(what).second)
            _queue.push(what);
    }

    void swap(SetQueue<QueueT> &oth) {
        _queue.swap(oth._queue);
        _queued.swap(oth._queued);
    }
};

} // namespace ADT
} // namespace dg

#endif // DG_ADT_QUEUE_H_
