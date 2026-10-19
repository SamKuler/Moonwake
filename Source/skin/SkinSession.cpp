#include "SkinSession.h"

#if JUCE_MAC
 #import <AppKit/AppKit.h>
#elif JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

SkinSession::SkinSession (std::shared_ptr<const CompiledSkin> compiled,
                          bool preview)
    : skin (std::move (compiled)), previewSession (preview)
{
    jassert (skin != nullptr && skin->assets != nullptr);
}

bool SkinSession::refreshMotionPreference()
{
   #if JUCE_MAC
    const auto reduced = [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion] != NO;
   #elif JUCE_WINDOWS
    BOOL animationsEnabled = TRUE;
    if (SystemParametersInfo (SPI_GETCLIENTAREAANIMATION, 0, &animationsEnabled, 0) == FALSE) return false;
    const auto reduced = animationsEnabled == FALSE;
   #else
    return false;
   #endif
   #if JUCE_MAC || JUCE_WINDOWS
    if (animationEngine.isReduceMotionEnabled() == reduced) return false;
    animationEngine.setReduceMotionEnabled (reduced);
    return true;
   #endif
}

std::shared_ptr<const CompiledSkin> SkinSession::compiledSkin() const
{
    return skin;
}

SkinSessionHealth SkinSession::health() const noexcept
{
    return currentHealth;
}

const SkinValidationReport& SkinSession::runtimeReport() const noexcept
{
    return report;
}

bool SkinSession::isPreview() const noexcept
{
    return previewSession;
}

SkinAnimationEngine& SkinSession::animations() noexcept
{
    return animationEngine;
}

SkinAssetCache& SkinSession::assets() const
{
    jassert (skin != nullptr && skin->assets != nullptr);
    return *skin->assets;
}

void SkinSession::commitPreview() noexcept
{
    previewSession = false;
}

void SkinSession::setFatalCallback (std::function<void()> callback)
{
    fatalCallback = std::move (callback);
}

void SkinSession::reportRenderFault (const SkinDiagnostic& diagnostic,
                                     SkinRenderStatus status)
{
    const auto key = diagnostic.code + juce::String::charToString (0x1f)
                   + diagnostic.file + juce::String::charToString (0x1f)
                   + diagnostic.pointer;
    if (diagnosticKeys.insert (key).second)
    {
        if (diagnostic.severity == SkinDiagnosticSeverity::error)
            report.addError (diagnostic.code, diagnostic.file, diagnostic.pointer,
                             diagnostic.message, diagnostic.suggestion);
        else
            report.addWarning (diagnostic.code, diagnostic.file, diagnostic.pointer,
                               diagnostic.message, diagnostic.suggestion);
    }

    if (status == SkinRenderStatus::fatalFault)
        currentHealth = SkinSessionHealth::fatal;
    else if (currentHealth == SkinSessionHealth::healthy)
        currentHealth = SkinSessionHealth::degraded;

    if (currentHealth == SkinSessionHealth::fatal && ! fatalCallbackSent)
    {
        fatalCallbackSent = true;
        if (fatalCallback) fatalCallback();
    }
}
