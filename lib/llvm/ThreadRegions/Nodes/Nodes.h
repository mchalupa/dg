#ifndef NODES_H
#define NODES_H

#include "CallFuncPtrNode.h"
#include "CallNode.h"
#include "CallReturnNode.h"
#include "EntryNode.h"
#include "ExitNode.h"
#include "ForkNode.h"
#include "GeneralNode.h"
#include "JoinNode.h"
#include "LockNode.h"
#include "ReturnNode.h"
#include "UnlockNode.h"

template <NodeType>
struct Map;

template <>
struct Map<NodeType::GENERAL> {
    using type = GeneralNode;
    using const_type = const GeneralNode;
};

template <>
struct Map<NodeType::FORK> {
    using type = ForkNode;
    using const_type = const ForkNode;
};

template <>
struct Map<NodeType::JOIN> {
    using type = JoinNode;
    using const_type = const JoinNode;
};

template <>
struct Map<NodeType::LOCK> {
    using type = LockNode;
    using const_type = const LockNode;
};

template <>
struct Map<NodeType::UNLOCK> {
    using type = UnlockNode;
    using const_type = const UnlockNode;
};

template <>
struct Map<NodeType::ENTRY> {
    using type = EntryNode;
    using const_type = const EntryNode;
};

template <>
struct Map<NodeType::EXIT> {
    using type = ExitNode;
    using const_type = const ExitNode;
};

template <>
struct Map<NodeType::CALL> {
    using type = CallNode;
    using const_type = const CallNode;
};

template <>
struct Map<NodeType::CALL_FUNCPTR> {
    using type = CallFuncPtrNode;
    using const_type = const CallFuncPtrNode;
};

template <>
struct Map<NodeType::CALL_RETURN> {
    using type = CallReturnNode;
    using const_type = const CallReturnNode;
};

template <>
struct Map<NodeType::RETURN> {
    using type = ReturnNode;
    using const_type = const ReturnNode;
};

template <NodeType nodeType, typename... Args>
typename Map<nodeType>::type *createNode(Args... args) {
    return new typename Map<nodeType>::type(args...);
}

template <NodeType nodeType, typename T>
typename Map<nodeType>::type *castNode(T *node) {
    if (!node) {
        return nullptr;
    }
    if (node->getType() == nodeType) {
        return static_cast<typename Map<nodeType>::type *>(node);
    }
    return nullptr;
}

template <NodeType nodeType, typename T>
typename Map<nodeType>::const_type *castNode(const T *node) {
    if (!node) {
        return nullptr;
    }
    if (node->getType() == nodeType) {
        return static_cast<typename Map<nodeType>::const_type *>(node);
    }
    return nullptr;
}

#endif // NODES_H
