#pragma once

#include "projecttreenode.h"

#include <memory>

// Shared by the project list view and the document-side project operations.
inline bool NodeIsGroup(std::shared_ptr<ProjectTreeNode> const& node)
{
    if (!node)
        return false;
    auto obj = node->representedObject();
    return obj && obj->isGroup();
}
