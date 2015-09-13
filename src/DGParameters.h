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

    void removeIn()
    {
        if (in) {
            in->isolate();
            delete in;
            in = nullptr;
        }
    }

    void removeOut()
    {
        if (out) {
            out->isolate();
            delete out;
            out = nullptr;
        }
    }
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
template <typename NodeT>
class DGParameters
{
public:
    typedef typename NodeT::KeyType KeyT;
    typedef std::map<KeyT, NodeT *> GlobalsContainerT;
    typedef std::map<KeyT, DGParameter<NodeT>> ContainerType;
    typedef typename ContainerType::iterator iterator;
    typedef typename ContainerType::const_iterator const_iterator;

    DGParameters<NodeT>()
    : BBIn(new BBlock<NodeT>), BBOut(new BBlock<NodeT>) {}

    ~DGParameters<NodeT>()
    {
        delete BBIn;
        delete BBOut;
    }

    NodeT& operator[](KeyT k) { return params[k]; }

    bool add(KeyT k, NodeT *val_in, NodeT *val_out)
    {
        auto v = std::make_pair(k, DGParameter<NodeT>(val_in, val_out));

        if (!params.insert(v).second)
            // we already has param with this key
            return false;

        // add in parameter into BBIn
        NodeT *last = BBIn->getLastNode();
        if (last) {
            last->setSuccessor(val_in);
            BBIn->setLastNode(val_in);
        } else { // BBIn is empty
            BBIn->setFirstNode(val_in);
            BBIn->setLastNode(val_in);
            val_in->setBasicBlock(BBIn);
        }

        // add in parameter into BBOut
        last = BBOut->getLastNode();
        if (last) {
            last->setSuccessor(val_out);
            BBOut->setLastNode(val_out);
        } else { // BBIn is empty
            BBOut->setFirstNode(val_out);
            BBOut->setLastNode(val_out);
            val_out->setBasicBlock(BBOut);
        }

        return true;
    }

    bool addGlobal(KeyT k, NodeT *val)
    {
        return globals.insert(std::make_pair(k, val)).second;
    }

    bool addGlobal(NodeT *val)
    {
        return addGlobal(val->getKeyT(), val);
    }

    NodeT *findGlobal(KeyT k)
    {
        auto it = globals.find(k);
        if (it == globals.end())
            return nullptr;

        return it->second;
    }

    GlobalsContainerT& getGlobals() { return globals; }
    const GlobalsContainerT& getGlobals() const { return globals; }

    void remove(KeyT k)
    {
        params.erase(k);
    }

    void removeIn(KeyT k)
    {
        DGParameter<NodeT> *p = find(k);
        if (!p)
            return;

        p->removeIn();

        // if we do not have the other, just remove whole param
        if (!p->out)
            params.erase(k);
    }

    void removeOut(KeyT k)
    {
        DGParameter<NodeT> *p = find(k);
        if (!p)
            return;

        p->removeOut();

        // if we do not have the other, just remove whole param
        if (!p->in)
            params.erase(k);
    }

    DGParameter<NodeT> *find(KeyT k)
    {
        iterator it = params.find(k);
        if (it == end())
            return nullptr;

        return &(it->second);
    }

    const DGParameter<NodeT> *find(KeyT k) const
    {
        return find(k);
    }

    size_t size() const { return params.size(); }

    iterator begin(void) { return params.begin(); }
    const_iterator begin(void) const { return params.begin(); }
    iterator end(void) { return params.end(); }
    const_iterator end(void) const { return params.end(); }

    const BBlock<NodeT> *getBBIn() const { return BBIn; }
    const BBlock<NodeT> *getBBOut() const { return BBOut; }
    BBlock<NodeT> *getBBIn() { return BBIn; }
    BBlock<NodeT> *getBBOut() { return BBOut; }

protected:
    // globals represented as a parameter
    GlobalsContainerT globals;
    // usual parameters
    ContainerType params;
    BBlock<NodeT> *BBIn;
    BBlock<NodeT> *BBOut;
};

} // namespace dg

#endif // _DG_PARAMETERS_H_
