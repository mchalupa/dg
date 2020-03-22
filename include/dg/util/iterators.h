#ifndef DG_ITERATORS_UTILS_H_
#define DG_ITERATORS_UTILS_H_

namespace dg {

template <typename It, typename Predicate>
struct iterator_filter : public It {
    It _current;
    It _end;
    const Predicate& pred;

    iterator_filter(const It& b, const It& e, const Predicate& p) : _current(b), _end(e), pred(p) {}
    template <typename Range>
    iterator_filter(Range& r, const Predicate& p) : _current(r.begin()), _end(r.end()), pred(p) {}

    iterator_filter& operator++() {
        It::operator++();
        while (_current != _end && !pred(It::operator*())) {
            It::operator++();
        }
        return *this;
    }

    iterator_filter operator++(int) {
        auto tmp = *this;
        operator++();
        return tmp;
    }

};

} // namespace dg

#endif
