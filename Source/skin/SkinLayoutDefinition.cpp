#include "SkinLayoutDefinition.h"

SkinScrollLayout::SkinScrollLayout (const SkinScrollLayout& other)
    : direction (other.direction),
      child (other.child != nullptr ? std::make_unique<SkinLayoutNode> (*other.child) : nullptr)
{
}

SkinScrollLayout& SkinScrollLayout::operator= (const SkinScrollLayout& other)
{
    if (this != &other)
    {
        direction = other.direction;
        child = other.child != nullptr ? std::make_unique<SkinLayoutNode> (*other.child) : nullptr;
    }
    return *this;
}

SkinScrollLayout::~SkinScrollLayout() = default;

SkinLayoutNodeType SkinLayoutNode::type() const
{
    return nodeType;
}
