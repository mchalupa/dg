#ifndef _DG_OFFSET_H_
#define _DG_OFFSET_H_

namespace dg {
namespace analysis {

#define UNKNOWN_OFFSET ~((uint64_t) 0)

// just a wrapper around uint64_t to
// handle UNKNOWN_OFFSET somehow easily
// maybe later we'll make it a range
struct Offset
{
    using type = uint64_t;

    Offset(type o = UNKNOWN_OFFSET) : offset(o) {}
    Offset& operator+=(const Offset& o)
    {
        if (offset == UNKNOWN_OFFSET)
            return *this;

        if (o.offset == UNKNOWN_OFFSET)
            offset = UNKNOWN_OFFSET;
        else
            offset += o.offset;

        return *this;
    }

    Offset operator+(const Offset& o)
    {
        if (offset == UNKNOWN_OFFSET || o.offset == UNKNOWN_OFFSET)
            return UNKNOWN_OFFSET;

        return Offset(offset + o.offset);
    }

    bool operator<(const Offset& o) const
    {
        return offset < o.offset;
    }

    bool operator==(const Offset& o) const
    {
        return offset == o.offset;
    }

    bool inRange(type from, type to) const
    {
        return (offset >= from && offset <= to);
    }

    bool isUnknown() const { return offset == UNKNOWN_OFFSET; }

    type operator*() const { return offset; }
    const type *operator->() const { return &offset; }

    type offset;
};

} // namespace analysis
} // namespace dg

#endif
