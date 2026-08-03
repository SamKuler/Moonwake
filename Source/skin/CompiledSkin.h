#pragma once

#include "SkinAssetCache.h"
#include "PlayerComponentCatalog.h"
#include "SkinLayoutDefinition.h"
#include "SkinManifest.h"
#include "SkinRenderDefinition.h"
#include "SkinTokenStore.h"

#include <map>
#include <memory>

struct CompiledSkin
{
    SkinManifest manifest;
    SkinTokenStore tokens;
    std::map<juce::String, SkinRenderStyleEntry> renderers;
    std::map<SkinLayoutEntry, SkinLayoutDefinition> layouts;
    std::shared_ptr<SkinAssetCache> assets;
    std::map<juce::String, std::shared_ptr<const SkinAssetCache>> assetCaches;

    [[nodiscard]] const SkinRenderStyleEntry* resolveStyle (
        SkinRenderTarget expectedTarget, const juce::String& requestedStyle) const
    {
        const auto matches = [expectedTarget] (const SkinRenderStyleEntry& entry)
        {
            return entry.renderer.target == expectedTarget;
        };
        if (requestedStyle.isNotEmpty())
            if (const auto found = renderers.find (requestedStyle);
                found != renderers.end() && matches (found->second))
                return &found->second;

        const auto fallback = renderers.find (
            skinRenderTargetName (expectedTarget) + ".default");
        return fallback != renderers.end() && matches (fallback->second)
            ? &fallback->second : nullptr;
    }

    [[nodiscard]] const SkinRenderStyleEntry* resolveSlotStyle (
        const SkinRenderStyleEntry& owner, juce::StringRef slotName,
        SkinRenderTarget expectedTarget) const
    {
        const auto found = owner.slotStyles.find (juce::String (slotName));
        return resolveStyle (expectedTarget,
                             found != owner.slotStyles.end() ? found->second
                                                             : juce::String());
    }
};
