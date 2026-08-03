#pragma once

#include "SkinPackage.h"
#include "SkinFontResolver.h"
#include "SkinValue.h"

#include <juce_graphics/juce_graphics.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace juce
{
class Drawable;
}

class SkinAssetCache final
{
public:
    struct Statistics
    {
        int assetFiles = 0;
        int64_t decodedPixels = 0;
        int64_t decodedBytes = 0;
        int svgElements = 0;
        int svgPathCommands = 0;
    };

    static constexpr int maximumAssetFiles = 512;
    static constexpr int maximumBitmapDimension = 4096;
    static constexpr int64_t maximumBitmapBytes = 64ll * 1024ll * 1024ll;
    static constexpr int64_t maximumPackagePixels = 32000000ll;
    static constexpr int64_t maximumDecodedBytes = 128ll * 1024ll * 1024ll;
    static constexpr int maximumSvgElements = 10000;
    static constexpr int maximumPackageSvgElements = 100000;
    static constexpr int maximumPackageSvgPathCommands = 250000;
    static constexpr int64_t maximumFontBytes = 12ll * 1024ll * 1024ll;

    static std::optional<std::shared_ptr<SkinAssetCache>> create (
        const SkinPackage&, juce::String skinId, juce::String skinVersion,
        const std::set<juce::String>& referencedAssets,
        const std::vector<SkinFontValue>& referencedFonts,
        SkinValidationReport&);

    [[nodiscard]] const juce::Image* findImage (juce::StringRef path) const;
    [[nodiscard]] std::shared_ptr<const juce::Drawable> findSvg (juce::StringRef path) const;
    [[nodiscard]] juce::Font makeFont (const SkinFontValue&, float height) const;
    [[nodiscard]] const juce::String& namespaceKey() const noexcept;
    [[nodiscard]] const Statistics& statistics() const noexcept;

private:
    SkinAssetCache() = default;

    juce::String cacheNamespace;
    Statistics stats;
    std::map<juce::String, juce::Image> images;
    std::map<juce::String, std::shared_ptr<const juce::Drawable>> svgs;
    std::map<juce::String, juce::Typeface::Ptr> typefaces;
    std::vector<SkinResolvedFontFace> resolvedFonts;
};
