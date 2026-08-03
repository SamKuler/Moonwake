#pragma once

#include "skin/SkinDiagnostic.h"

#include <juce_gui_basics/juce_gui_basics.h>

class SkinFallbackDiagnostic final : public juce::Component
{
public:
    SkinFallbackDiagnostic();
    ~SkinFallbackDiagnostic() override;

    void setMessage (const juce::String&);
    [[nodiscard]] static juce::String makeStartupStatusText (
        const SkinValidationReport&, const juce::String& normalStatus);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::LookAndFeel_V4 fallbackLookAndFeel;
    juce::Label messageLabel;
};
