/// XXX add licence
//

#ifndef _DG_PARAMETERS_H_
#define _DG_PARAMETERS_H_

#include <map>
#include <utility>

#include "BBlock.h"

namespace dg {

template <typename NodeT>
struct DGParameter
{
    DGParameter<NodeT>(NodeT *v1, NodeT *v2)
        : in(v1), out(v2) {}

    // input value of parameter
    NodeT *in;
    // output value of parameter
    NodeT *out;
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
template <typename Key, typename NodeT>
class DGParameters
{
public:
    typedef std::map<Key, DGParameter<NodeT>> ContainerType;
    typedef typename ContainerType::iterator iterator;
    typedef typename ContainerType::const_iterator const_iterator;

    NodeT& operator[](Key k) { return params[k]; }
    bool add(Key k, NodeT *val_in, NodeT *val_out)
    {
        auto v = std::make_pair(k, DGParameter<NodeT>(val_in, val_out));

        if (!params.insert(v).second)
            // we already has param with this key
            return false;

        // add in parameter into BBIn
        NodeT *last = BBIn.getLastNode();
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

    const BBlock<NodeT> *getBBIn() const { return &BBIn; }
    const BBlock<NodeT> *getBBOut() const { return &BBOut; }

protected:
    ContainerType params;
    BBlock<NodeT> BBIn;
    BBlock<NodeT> BBOut;
};

} // namespace dg

#endif // _DG_PARAMETERS_H_
