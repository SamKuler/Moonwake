#include "AlbumArtworkComponent.h"

#include "JuceSkinAdapter.h"

void AlbumArtworkComponent::setTrack (juce::String stableIdToUse,
                                      juce::String titleToUse,
                                      const juce::File& artworkFile)
{
    const auto nextPath = artworkFile.getFullPathName();
    if (stableId == stableIdToUse && title == titleToUse
        && artworkPath == nextPath)
        return;

    stableId = std::move (stableIdToUse);
    title = std::move (titleToUse);
    artworkPath = nextPath;
    artwork = artworkFile.existsAsFile()
                  ? juce::ImageFileFormat::loadFrom (artworkFile)
                  : juce::Image{};
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
    context.bounds = getLocalBounds().toFloat();
    context.contentBounds = context.bounds;
    const auto aspectRatio = artwork.isValid() && artwork.getHeight() > 0
                               ? static_cast<double> (artwork.getWidth())
                                   / artwork.getHeight()
                               : 1.0;
    context.extraLookup = [hasImage = artwork.isValid(), aspectRatio] (
                              std::string_view path)
        -> std::optional<SkinExpressionValue>
    {
        if (path == "artwork.hasImage") return SkinExpressionValue (hasImage);
        if (path == "artwork.aspectRatio") return SkinExpressionValue (aspectRatio);
        return std::nullopt;
    };
    static_cast<void> (adapter->drawCustomSlot (
        graphics, *this, SkinRenderTarget::artwork, "frame.background",
        SkinRenderTarget::surface, context, "cover:background"));
    static_cast<void> (adapter->drawCustomComponent (
        graphics, *this, SkinRenderTarget::artwork, context, "cover"));

    if (artwork.isValid())
    {
        const auto presentation = getProperties().getWithDefault (
            JuceSkinAdapter::presentationProperty, "contain").toString();
        const juce::RectanglePlacement placement (
            presentation == "cover"
                ? juce::RectanglePlacement::fillDestination
                : juce::RectanglePlacement::centred);
        const auto imageStyle = adapter->artworkImageStyleFor (*this, context);
        const auto imageBounds = imageStyle ? imageStyle->bounds : context.bounds;
        juce::Graphics::ScopedSaveState savedState (graphics);
        if (imageStyle && imageStyle->clip)
        {
            juce::Path clipPath;
            clipPath.addRoundedRectangle (imageBounds, imageStyle->cornerRadius);
            graphics.reduceClipRegion (clipPath);
        }
        graphics.drawImageTransformed (
            artwork,
            placement.getTransformToFit (artwork.getBounds().toFloat(), imageBounds),
            false);
    }

    static_cast<void> (adapter->drawCustomSlot (
        graphics, *this, SkinRenderTarget::artwork, "frame.overlay",
        SkinRenderTarget::surface, std::move (context), "cover:overlay"));
}
