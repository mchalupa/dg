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

    DGParameter<NodeT> *operator[](KeyT k) { return find(k); }
    const DGParameter<NodeT> *operator[](KeyT k) const { return find(k); }

    bool add(KeyT k, NodeT *val_in, NodeT *val_out)
    {
        return add(k, val_in, val_out, &params);
    }

    bool addGlobal(KeyT k, NodeT *val_in, NodeT *val_out)
    {
        return add(k, val_in, val_out, &globals);
    }

    DGParameter<NodeT> *findGlobal(KeyT k)
    {
        return find(k, &globals);
    }

    DGParameter<NodeT> *findParameter(KeyT k)
    {
        return find(k, &params);
    }

    DGParameter<NodeT> *find(KeyT k)
    {
        DGParameter<NodeT> *ret = findParameter(k);
        if (!ret)
            return findGlobal(k);

        return ret;
    }

    const DGParameter<NodeT> *findParameter(KeyT k) const { return findParameter(k); }
    const DGParameter<NodeT> *findGlobal(KeyT k) const { return findGlobal(k); }
    const DGParameter<NodeT> *find(KeyT k) const { return find(k); }

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

    size_t paramsNum() const { return params.size(); }
    size_t globalsNum() const { return globals.size(); }
    size_t size() const { return params.size() + globals.size(); }

    iterator begin(void) { return params.begin(); }
    const_iterator begin(void) const { return params.begin(); }
    iterator end(void) { return params.end(); }
    const_iterator end(void) const { return params.end(); }

    iterator global_begin() { return globals.begin(); }
    const_iterator global_begin() const { return globals.begin(); }
    iterator global_end() { return globals.end(); }
    const_iterator global_end() const { return globals.end(); }

    const BBlock<NodeT> *getBBIn() const { return BBIn; }
    const BBlock<NodeT> *getBBOut() const { return BBOut; }
    BBlock<NodeT> *getBBIn() { return BBIn; }
    BBlock<NodeT> *getBBOut() { return BBOut; }

private:
    // globals represented as a parameter
    ContainerType globals;
    // usual parameters
    ContainerType params;

    BBlock<NodeT> *BBIn;
    BBlock<NodeT> *BBOut;

    DGParameter<NodeT> *find(KeyT k, ContainerType *C)
    {
        iterator it = C->find(k);
        if (it == C->end())
            return nullptr;

        return &(it->second);
    }

    bool add(KeyT k, NodeT *val_in, NodeT *val_out, ContainerType *C)
    {
        auto v = std::make_pair(k, DGParameter<NodeT>(val_in, val_out));
        if (!C->insert(v).second)
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
};

} // namespace dg

#endif // _DG_PARAMETERS_H_
