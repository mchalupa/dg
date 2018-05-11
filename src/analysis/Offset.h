#ifndef _DG_OFFSET_H_
#define _DG_OFFSET_H_

#include <cstdint>

namespace dg {
namespace analysis {

// just a wrapper around uint64_t to
// handle Offset::UNKNOWN somehow easily
// maybe later we'll make it a range
struct Offset
{
    using type = uint64_t;

    // the value used for the unknown offset
    static const type UNKNOWN;

    // cast to type
    //operator type() { return offset; }

    Offset(type o = UNKNOWN) : offset(o) {}
    Offset(const Offset&) = default;

    Offset operator+(const Offset o) const
    {
        if (offset == UNKNOWN || o.offset == UNKNOWN ||
            offset >= UNKNOWN - o.offset) {
            return UNKNOWN;
        }

        return Offset(offset + o.offset);
    }

    Offset operator-(const Offset& o) const
    {
        if (offset == UNKNOWN || o.offset == UNKNOWN ||
            offset < o.offset) {
            return Offset(UNKNOWN);
        }

        return Offset(offset - o.offset);
    }

    bool operator<(const Offset& o) const
    {
        return offset < o.offset;
    }

    bool operator>(const Offset& o) const
    {
        return offset > o.offset;
    }

    bool operator<=(const Offset& o) const
    {
        return offset <= o.offset;
    }

    bool operator>=(const Offset& o) const
    {
        return offset >= o.offset;
    }

    bool operator==(const Offset& o) const
    {
        return offset == o.offset;
    }

    bool operator!=(const Offset& o) const
    {
        return offset != o.offset;
    }

    bool inRange(type from, type to) const
    {
        return (offset >= from && offset <= to);
    }

    bool isUnknown() const { return offset == UNKNOWN; }
    bool isZero() const { return offset == 0; }

    type operator*() const { return offset; }
    const type *operator->() const { return &offset; }

    type offset;
};

} // namespace analysis
} // namespace dg

#endif
