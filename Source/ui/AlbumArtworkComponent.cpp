#include "AlbumArtworkComponent.h"

#include "JuceSkinAdapter.h"

void AlbumArtworkComponent::setTrack (juce::String stableIdToUse,
                                      juce::String titleToUse)
{
    if (stableId == stableIdToUse && title == titleToUse)
        return;

    stableId = std::move (stableIdToUse);
    title = std::move (titleToUse);
    repaint();
}

void AlbumArtworkComponent::paint (juce::Graphics& graphics)
{
    auto* adapter = dynamic_cast<JuceSkinAdapter*> (&getLookAndFeel());
    if (adapter == nullptr)
        return;

    SkinRenderContext context;
    context.primaryText = title;
    context.secondaryText = stableId;
    context.iconName = stableId.isEmpty() ? "empty" : "track";
    context.extraLookup = [hasImage = stableId.isNotEmpty()] (
                              std::string_view path)
        -> std::optional<SkinExpressionValue>
    {
        if (path == "artwork.hasImage") return SkinExpressionValue (hasImage);
        if (path == "artwork.aspectRatio") return SkinExpressionValue (1.0);
        return std::nullopt;
    };
    static_cast<void> (adapter->drawCustomComponent (
        graphics, *this, SkinRenderTarget::artwork, std::move (context), "cover"));
}
