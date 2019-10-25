#ifndef _DG_CONTROL_EXPRESSION_H_
#define _DG_CONTROL_EXPRESSION_H_

#include <list>
#include <vector>
#include <set>
//#include <iostream>
#include <algorithm>
#include <cassert>

#include "CENode.h"

namespace dg {

//template <typename T>
class ControlExpression {
    CENode *root;

public:
    using CEPath = std::vector<CENode *>;

    ControlExpression(CENode *r)
        : root(r) {}

    ControlExpression()
        :root(nullptr) {}

    ControlExpression(ControlExpression&& oth)
        :root(oth.root)
    {
        oth.root = nullptr;
    }

    ControlExpression& operator=(ControlExpression&& oth)
    {
        root = oth.root;
        oth.root = nullptr;
        return *this;
    }

    ControlExpression(const ControlExpression& oth) = delete;
    /*
        : root(oth.root->clone())
    {
    }
    */

    CENode *getRoot()
    {
        return root;
    }

    void computeSets()
    {
        root->computeSets();
    }

    /*
    void dump() const
    {
        root->dump();
    }
    */

    // return by value to allow using
    // move constructor
    template <typename T>
    std::vector<CENode *> getLabels(const T& lab) const
    {
        std::vector<CENode *> tmp;
        getLabels(root, lab, tmp);
        return tmp;
    }

    template <typename T>
    std::vector<CEPath> getPathsFrom(const T& l) const
    {
        std::vector<CEPath> paths;
        std::vector<CENode *> labels = getLabels<T>(l);

        for (CENode *lab : labels) {
            CEPath P;
            for (auto I = lab->path_begin(), E = lab->path_end();
                 I != E; ++I) {
                P.push_back(*I);

                // XXX: Consider selective termination
                // every loop may non-terminate, so every loop terminates a path
                if ((*I)->isa(CENodeType::LOOP))
                    // copy the path that is terminated with this loop
                    paths.push_back(CEPath(P));

                // continue building the path
            }

            // rely on move constructors
            paths.push_back(P);
        }

        // hopefully a move constructor again
        return paths;
    }

    // FIXME: this should go out of this class
    std::pair<CENode::VisitsSetT, CENode::VisitsSetT>
    getSetsForPath(CEPath& path, bool termination_sensitive = false) const
    {
        CENode::VisitsSetT always, smtm;
        bool found_loop = false;

        for (CENode *nd : path) {
            if (nd->isa(CENodeType::LOOP))
                found_loop = true;

            // in the case we compute termination sensitive
            // information, we assume that a loop may
            // not terminate, therefore everything that
            // follows a loop is only 'sometimes' (possibly) visited
            if (found_loop && termination_sensitive) {
                smtm.insert(nd->getAlwaysVisits().begin(),
                            nd->getAlwaysVisits().end());
            } else {
                always.insert(nd->getAlwaysVisits().begin(),
                              nd->getAlwaysVisits().end());
            }

            smtm.insert(nd->getSometimesVisits().begin(),
                        nd->getSometimesVisits().end());
        }

        CENode::VisitsSetT diff;
        std::set_difference(smtm.begin(), smtm.end(),
                            always.begin(), always.end(),
                            std::inserter(diff, diff.end()), CENode::CECmp());

        diff.swap(smtm);

        return std::make_pair(always, smtm);
    }

    std::pair<CENode::VisitsSetT, CENode::VisitsSetT>
    getSets(std::vector<CEPath>& paths, bool termination_sensitive = false) const
    {
        CENode::VisitsSetT always, smtm;

        assert(paths.size() > 0);

        if (paths.size() == 1)
            return getSetsForPath(*(paths.begin()), termination_sensitive);

        auto I = paths.begin();
        auto S = getSetsForPath(*I, termination_sensitive);

        // initialize the alwas set
        always = std::move(S.first);

        // initialize the somtimes set (we put there everything
        // and then filter it at the end)
        smtm = std::move(S.second);
        smtm.insert(always.begin(), always.end());

        ++I;
        for (auto E = paths.end(); I != E; ++I) {
            // make intersection of the always sets
            CENode::VisitsSetT tmpa;

            auto cur = getSetsForPath(*I, termination_sensitive);
            std::set_intersection(always.begin(), always.end(),
                                  cur.first.begin(), cur.first.end(),
                                  std::inserter(tmpa, tmpa.end()),
                                  CENode::CECmp());

            always.swap(tmpa);

            // we insert both sets into the insert and then
            // we simply remove what is in always
            smtm.insert(cur.first.begin(), cur.first.end());
            smtm.insert(cur.second.begin(), cur.second.end());
        }

        CENode::VisitsSetT diff;
        std::set_difference(smtm.begin(), smtm.end(),
                            always.begin(), always.end(),
                            std::inserter(diff, diff.end()), CENode::CECmp());

        diff.swap(smtm);

        return std::make_pair(always, smtm);
    }

    template <typename T>
    CENode::VisitsSetT getControlScope(const T& lab,
                                       bool termination_sensitive = false) const
    {
        assert((!root->getAlwaysVisits().empty() ||
               !root->getSometimesVisits().empty()) && "Did you called computeSets?");

        auto paths = getPathsFrom<T>(lab);
        // return the 'sometimes' set
        return getSets(paths, termination_sensitive).second;
    }

private:

    template <typename T>
    void getLabels(CENode *nd, const T& lab, std::vector<CENode *>& out) const
    {
        if (nd->isLabel()) {
            CELabel<T> *l = static_cast<CELabel<T> *>(nd);
            // std::cout << l->getLabel() << " == " << lab << "\n";
            if (l->getLabel() == lab) {
                out.push_back(nd);
            }
        }

        // recursively call to children
        for (CENode *chld : nd->getChildren())
            getLabels(chld, lab, out);
    }
};

} // namespace dg

#endif // _DG_CONTROL_EXPRESSION_H_
