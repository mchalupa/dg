/// XXX add licence
//

#ifndef _DG_PARAMETERS_H_
#define _DG_PARAMETERS_H_

#include <map>
#include <utility>

namespace dg {

template <typename ValueType>
struct DGParameter
{
    DGParameter<ValueType>(const ValueType& v1,
                           const ValueType& v2)
        : in(v1), out(v2) {}

    // input value of parameter
    ValueType in;
    // output value of parameter
    ValueType out;
};

// --------------------------------------------------------
// --- Parameters of functions
// --------------------------------------------------------
template <typename Key, typename ValueType>
class DGParameters
{
public:
    typedef std::map<Key, DGParameter<ValueType>> ContainerType;
    typedef typename ContainerType::iterator iterator;
    typedef typename ContainerType::const_iterator const_iterator;

    ValueType& operator[](Key k) { return params[k]; }
    bool add(Key k, ValueType val_in, ValueType val_out)
    {
        auto v = std::make_pair(k, DGParameter<ValueType>(val_in, val_out));
        return params.insert(v).second;
    }

    iterator begin(void) { return params.begin(); }
    const_iterator begin(void) const { return params.begin(); }
    iterator end(void) { return params.end(); }
    const_iterator end(void) const { return params.end(); }

protected:
    ContainerType params;
};

} // namespace dg

#endif // _DG_PARAMETERS_H_
