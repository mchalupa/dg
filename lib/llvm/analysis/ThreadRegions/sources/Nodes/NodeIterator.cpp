#include "NodeIterator.h"

#include "Nodes.h"

NodeIterator::NodeIterator(const Node *node, bool begin) {
    if (!node) {
        return;
    }
    if (begin) {
        if ((forkNode = castNode<NodeType::FORK>(node))) {
            forkSuccessorsIterator = forkNode->forkSuccessors().begin();
        } else if ((exitNode = castNode<NodeType::EXIT>(node))) {
            joinSuccessorsIterator = exitNode->joinSuccessors().begin();
        }
        successorsIterator = node->successors().begin();
    } else {
        if ((forkNode = castNode<NodeType::FORK>(node))) {
            forkSuccessorsIterator = forkNode->forkSuccessors().end();
        } else if ((exitNode = castNode<NodeType::EXIT>(node))) {
            joinSuccessorsIterator = exitNode->joinSuccessors().end();
        }
        successorsIterator = node->successors().end();
    }
}

NodeIterator &NodeIterator::operator++() {
    if (forkNode) {
        if (forkSuccessorsIterator != forkNode->forkSuccessors().end()) {
            ++forkSuccessorsIterator;
            return *this;
        }
    } else if (exitNode) {
        if (joinSuccessorsIterator != exitNode->joinSuccessors().end()) {
            ++joinSuccessorsIterator;
            return *this;
        }
    }
    ++successorsIterator;
    return *this;
}

NodeIterator NodeIterator::operator++(int) {
    NodeIterator retval = *this;
    ++(*this);
    return retval;
}

bool NodeIterator::operator==(const NodeIterator &other) const {
    if (successorsIterator != other.successorsIterator) {
        return false;
    }

    if (forkNode) {
        return forkSuccessorsIterator == other.forkSuccessorsIterator;
    } else if (exitNode) {
        return joinSuccessorsIterator == other.joinSuccessorsIterator;
    }
    return true;
}

bool NodeIterator::operator!=(const NodeIterator &other) const {
    return !(*this == other);
}

Node *NodeIterator::operator*() const {
    if (forkNode) {
        if (forkSuccessorsIterator != forkNode->forkSuccessors().end()) {
            return *forkSuccessorsIterator;
        }
    } else if (exitNode) {
        if (joinSuccessorsIterator != exitNode->joinSuccessors().end()) {
            return *joinSuccessorsIterator;
        }
    }
    return  *successorsIterator;
}
