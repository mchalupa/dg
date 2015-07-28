/// XXX add licence
//

#ifndef _DG_PARAMETERS_H_
#define _DG_PARAMETERS_H_

#include <map>
#include <utility>

#include "BBlock.h"

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
//
//  DGParameters keep list of function parameters (arguments).
//  Each parameter is a pair - input and output value and is
//  represented as a node in the dependence graph.
//  Moreover, there are BBlocks for input and output parameters
//  so that the parameters can be used in BBlock analysis
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

        if (!params.insert(v).second)
            // we already has param with this key
            return false;

        // add in parameter into BBIn
        ValueType last = BBIn.getLastNode();
        if (last)
            last->setSuccessor(val_in);
        else { // BBIn is empty
            BBIn.setFirstNode(val_in);
            BBIn.setLastNode(val_in);
            val_in->setBasicBlock(&BBIn);
        }

        // add in parameter into BBOut
        last = BBOut.getLastNode();
        if (last)
            last->setSuccessor(val_out);
        else { // BBIn is empty
            BBOut.setFirstNode(val_out);
            BBOut.setLastNode(val_out);
            val_out->setBasicBlock(&BBOut);
        }

        return true;
    }

    iterator begin(void) { return params.begin(); }
    const_iterator begin(void) const { return params.begin(); }
    iterator end(void) { return params.end(); }
    const_iterator end(void) const { return params.end(); }

    const BBlock<ValueType> *getBBIn() const { return &BBIn; }
    const BBlock<ValueType> *getBBOut() const { return &BBOut; }

protected:
    ContainerType params;
    BBlock<ValueType> BBIn;
    BBlock<ValueType> BBOut;
};

} // namespace dg

#endif // _DG_PARAMETERS_H_
