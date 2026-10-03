#include "AlbumArtworkComponent.h"

#include "JuceSkinAdapter.h"

void AlbumArtworkComponent::setImage (juce::String stableIdToUse,
                                     juce::String titleToUse, juce::Image image)
{
    if (stableId == stableIdToUse && title == titleToUse && artwork == image
        && artworkPath.isEmpty()) return;
    stableId = std::move (stableIdToUse);
    title = std::move (titleToUse);
    artworkPath.clear();
    artwork = std::move (image);
    ambience = {};
    repaint();
}

void AlbumArtworkComponent::setTrack (juce::String stableIdToUse,
                                      juce::String titleToUse,
                                      const juce::File& artworkFile)
{
    const auto nextPath = artworkFile.getFullPathName();
    if (stableId == stableIdToUse && title == titleToUse
        && artworkPath == nextPath)
        return;

    const auto artworkChanged = stableId != stableIdToUse || artworkPath != nextPath;
    stableId = std::move (stableIdToUse);
    title = std::move (titleToUse);
    artworkPath = nextPath;
    if (artworkChanged)
    {
        artwork = artworkFile.existsAsFile()
                      ? juce::ImageFileFormat::loadFrom (artworkFile)
                      : juce::Image{};
        ambience = {};
        if (artwork.isValid())
        {
            constexpr int maximumDimension = 64;
            const auto scale = juce::jmin (1.0, static_cast<double> (maximumDimension)
                / juce::jmax (artwork.getWidth(), artwork.getHeight()));
            const auto source = artwork.rescaled (
                juce::jmax (1, juce::roundToInt (artwork.getWidth() * scale)),
                juce::jmax (1, juce::roundToInt (artwork.getHeight() * scale)),
                juce::Graphics::highResamplingQuality).convertedToFormat (juce::Image::ARGB);
            ambience = juce::Image (juce::Image::ARGB, source.getWidth(), source.getHeight(), true);
            juce::ImageConvolutionKernel blur (33);
            blur.createGaussianBlur (8.0f);
            blur.applyToImage (ambience, source, source.getBounds());
        }
    }
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
    context.extraLookup = [hasTrack = stableId.isNotEmpty(),
                           hasImage = artwork.isValid(), aspectRatio, additional = extraContext] (
                              std::string_view path)
        -> std::optional<SkinExpressionValue>
    {
        if (path == "artwork.hasTrack") return SkinExpressionValue (hasTrack);
        if (path == "artwork.hasImage") return SkinExpressionValue (hasImage);
        if (path == "artwork.aspectRatio") return SkinExpressionValue (aspectRatio);
        return additional ? additional (path) : std::nullopt;
    };
    static_cast<void> (adapter->drawCustomSlot (
        graphics, *this, SkinRenderTarget::artwork, "frame.background",
        SkinRenderTarget::surface, context, "cover:background"));
    if (ambience.isValid())
    {
        if (const auto style = adapter->artworkAmbienceStyleFor (*this, context);
            style && style->visible && style->opacity > 0.0f)
        {
            juce::Graphics::ScopedSaveState savedState (graphics);
            juce::Path clipPath;
            clipPath.addRoundedRectangle (style->bounds, style->cornerRadius);
            graphics.reduceClipRegion (clipPath);
            graphics.setOpacity (style->opacity);
            graphics.drawImageTransformed (ambience,
                juce::RectanglePlacement (juce::RectanglePlacement::fillDestination)
                    .getTransformToFit (ambience.getBounds().toFloat(), style->bounds), false);
        }
    }
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
