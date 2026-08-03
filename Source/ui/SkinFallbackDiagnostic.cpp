#include "SkinFallbackDiagnostic.h"

SkinFallbackDiagnostic::SkinFallbackDiagnostic()
{
    messageLabel.setLookAndFeel (&fallbackLookAndFeel);
    messageLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (messageLabel);
}

SkinFallbackDiagnostic::~SkinFallbackDiagnostic()
{
    messageLabel.setLookAndFeel (nullptr);
}

void SkinFallbackDiagnostic::setMessage (const juce::String& message)
{
    messageLabel.setText ("The player skin could not be loaded.\n\n" + message,
                          juce::dontSendNotification);
}

juce::String SkinFallbackDiagnostic::makeStartupStatusText (
    const SkinValidationReport& report, const juce::String& normalStatus)
{
    if (! report.hasErrors())
        return normalStatus;

    for (const auto& diagnostic : report.getDiagnostics())
        if (diagnostic.severity == SkinDiagnosticSeverity::error)
            return "Skin fallback active: " + diagnostic.message
                 + " [" + diagnostic.code + "]";

    return "Skin fallback active.";
}

void SkinFallbackDiagnostic::paint (juce::Graphics& graphics)
{
    graphics.fillAll (fallbackLookAndFeel.findColour (
        juce::ResizableWindow::backgroundColourId));
}

void SkinFallbackDiagnostic::resized()
{
    messageLabel.setBounds (getLocalBounds().reduced (32));
}
