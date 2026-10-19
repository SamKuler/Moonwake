#pragma once

#include "CompiledSkin.h"
#include "SkinAnimationEngine.h"
#include "SkinRenderer.h"

#include <functional>
#include <set>

enum class SkinSessionHealth { healthy, degraded, fatal };

class SkinSession final : public SkinRuntimeFaultSink
{
public:
    SkinSession (std::shared_ptr<const CompiledSkin>, bool preview);

    [[nodiscard]] std::shared_ptr<const CompiledSkin> compiledSkin() const;
    [[nodiscard]] SkinSessionHealth health() const noexcept;
    [[nodiscard]] const SkinValidationReport& runtimeReport() const noexcept;
    [[nodiscard]] bool isPreview() const noexcept;
    SkinAnimationEngine& animations() noexcept;
    bool refreshMotionPreference();
    SkinAssetCache& assets() const;

    void commitPreview() noexcept;
    void setFatalCallback (std::function<void()>);
    void reportRenderFault (const SkinDiagnostic&, SkinRenderStatus) override;

private:
    std::shared_ptr<const CompiledSkin> skin;
    SkinAnimationEngine animationEngine;
    SkinValidationReport report;
    SkinSessionHealth currentHealth = SkinSessionHealth::healthy;
    bool previewSession = false;
    bool fatalCallbackSent = false;
    std::set<juce::String> diagnosticKeys;
    std::function<void()> fatalCallback;
};
