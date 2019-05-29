#ifndef _DG_PARAMETERS_H_
#define _DG_PARAMETERS_H_

#include <map>
#include <memory>
#include <utility>

#include "BBlock.h"

namespace dg {

enum class DGParametersType {
    ACTUAL,
    FORMAL
};

template <typename NodeT> class DGParameters;

template <typename NodeT>
struct DGParameterPair
{
    DGParameterPair<NodeT>(NodeT *v1, NodeT *v2,
                           DGParameters<NodeT> *p)
        : in(v1), out(v2), parent(p) {}

    // input value of parameter
    NodeT *in;
    // output value of parameter
    NodeT *out;

    DGParameters<NodeT> *parent;

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

    DGParameters<NodeT> *getParent() { return parent; }
    const DGParameters<NodeT> *getParent() const { return parent; }
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
    DGParametersType type;

protected:

    DGParameters<NodeT>(DGParametersType t)
    : type(t), BBIn(new BBlock<NodeT>), BBOut(new BBlock<NodeT>) {}

public:
    using KeyT = typename NodeT::KeyType;
    using ContainerType = std::map<KeyT, DGParameterPair<NodeT>>;
    using iterator = typename ContainerType::iterator;
    using const_iterator = typename ContainerType::const_iterator;

    ~DGParameters<NodeT>()
    {
        // delete the parameters itself
        for (const auto& par : *this) {
            delete par.second.in;
            delete par.second.out;
        }

        // delete globals parameters
        for (const auto& gl : globals) {
            delete gl.second.in;
            delete gl.second.out;
        }

#ifdef ENABLE_CFG
        // delete auxiliary basic blocks
        delete BBIn;
        delete BBOut;
#endif
    }

    DGParameterPair<NodeT> *operator[](KeyT k) { return find(k); }
    const DGParameterPair<NodeT> *operator[](KeyT k) const { return find(k); }

    template <typename... Args>
    std::pair<NodeT *, NodeT *>
    construct(KeyT k, Args... args) {
        auto in = new NodeT(args...);
        auto out = new NodeT(args...);
        add(k, in, out, &params);
        return {in, out};
    }

    template <typename... Args>
    std::pair<NodeT *, NodeT *>
    constructGlobal(KeyT k, Args... args) {
        auto in = new NodeT(args...);
        auto out = new NodeT(args...);
        add(k, in, out, &globals);
        return {in, out};
    }

    DGParameterPair<NodeT> *findGlobal(KeyT k)
    {
        return find(k, &globals);
    }

    DGParameterPair<NodeT> *findParameter(KeyT k)
    {
        return find(k, &params);
    }

    DGParameterPair<NodeT> *find(KeyT k)
    {
        auto ret = findParameter(k);
        if (!ret)
            return findGlobal(k);

        return ret;
    }

    const DGParameterPair<NodeT> *findParameter(KeyT k) const
    {
        return findParameter(k);
    }

    const DGParameterPair<NodeT> *findGlobal(KeyT k) const { return findGlobal(k); }
    const DGParameterPair<NodeT> *find(KeyT k) const { return find(k); }

    void remove(KeyT k)
    {
        params.erase(k);
    }

    void removeIn(KeyT k)
    {
        auto p = find(k);
        if (!p)
            return;

        p->removeIn();

        // if we do not have the other, just remove whole param
        if (!p->out)
            params.erase(k);
    }

    void removeOut(KeyT k)
    {
        auto p = find(k);
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

    DGParameterPair<NodeT>* getVarArg() { return vararg.get(); }
    const DGParameterPair<NodeT>* getVarArg() const { return vararg.get(); }
    bool setVarArg(NodeT *in, NodeT *out)
    {
        assert(!vararg && "Already has a vararg parameter");

        vararg.reset(new DGParameterPair<NodeT>(in, out, this));
        return true;
    }

    NodeT* getNoReturn() { return noret.get(); }
    const NodeT* getNoReturn() const { return noret.get(); }
    bool addNoReturn(NodeT *n)
    {
        assert(!noret && "Already has the noret parameter");

        noret.reset(n);
        return true;
    }

    DGParametersType getParametersType() const { return type; }

private:
    // globals represented as a parameter
    ContainerType globals;
    // usual parameters
    ContainerType params;

    // this is parameter that represents
    // formal vararg parameters. It is only one, because without
    // any further analysis, we cannot tell apart the formal varargs
    std::unique_ptr<DGParameterPair<NodeT>> vararg{};
    // node representing that the function may not return
    // -- we can add control dependencies to this node
    std::unique_ptr<NodeT> noret{};

    BBlock<NodeT> *BBIn;
    BBlock<NodeT> *BBOut;
    NodeT *callSite;

    DGParameterPair<NodeT> *find(KeyT k, ContainerType *C)
    {
        iterator it = C->find(k);
        if (it == C->end())
            return nullptr;

        return &(it->second);
    }

    bool add(KeyT k, NodeT *val_in, NodeT *val_out, ContainerType *C)
    {
        auto v = std::make_pair(k, DGParameterPair<NodeT>(val_in, val_out, this));
        if (!C->insert(v).second)
            // we already has param with this key
            return false;

        BBIn->append(val_in);
        BBOut->append(val_out);

        return true;
    }
};

template <typename NodeT>
class ActualDGParameters : public DGParameters<NodeT> {
    NodeT *callSite{nullptr};

public:
    ActualDGParameters(NodeT *cs)
    : DGParameters<NodeT>(DGParametersType::ACTUAL), callSite(cs) {}

    const NodeT *getCallSite() const { return callSite; }
    NodeT *getCallSite() { return callSite; }
    //void setCallSite(NodeT *n) { return callSite = n; }

    static ActualDGParameters *get(DGParameters<NodeT> *p) {
        return p->getParametersType() == DGParametersType::ACTUAL ?
                static_cast<ActualDGParameters *>(p) : nullptr;
    }
};

template <typename NodeT>
class FormalDGParameters : public DGParameters<NodeT> {
    using DependenceGraphT = typename NodeT::DependenceGraphType;


    DependenceGraphT *dg{nullptr};
public:

    // mapping of formal parameters to (callSite, actual parameter)
    std::map<NodeT *, std::map<NodeT*, NodeT*>> formalToActual;

    FormalDGParameters(DependenceGraphT *g)
    : DGParameters<NodeT>(DGParametersType::FORMAL), dg(g) {}

    const DependenceGraphT *getDG() const { return dg; }
    DependenceGraphT *getDG() { return dg; }

    static FormalDGParameters *get(DGParameters<NodeT> *p) {
        return p->getParametersType() == DGParametersType::FORMAL ?
                static_cast<FormalDGParameters *>(p) : nullptr;
    }
};

} // namespace dg

#endif // _DG_PARAMETERS_H_
