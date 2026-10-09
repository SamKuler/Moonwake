#pragma once

#include "audio/OutputCalibration.h"
#include "skin/SkinRowLayout.h"
#include <juce_gui_basics/juce_gui_basics.h>

class OutputCalibrationComponent final : public juce::Component
{
public:
    OutputCalibrationComponent();
    ~OutputCalibrationComponent() override;
    void setState (OutputLayout, const std::vector<OutputChannelCalibration>&, CalibrationTestSignal);
    void discardEdits();
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<juce::Result (const OutputChannelCalibration&)> onChange;
    // Only an explicit Start sets starting; successful callbacks return the accepted configuration.
    std::function<juce::Result (CalibrationTestSignal&, bool starting)> onTestChange;

private:
    void selectChannel();
    void refreshChannel();
    bool commitCalibration();
    bool commitTestParameters (bool starting = false);
    void toggleTest();
    bool sendTest (CalibrationTestSignal);
    void refreshTest();
    void showError (juce::String);
    void escapeEditing();
    void reveal (juce::Component&);
    [[nodiscard]] juce::AudioChannelSet::ChannelType selectedChannel() const;

    class Editor final : public juce::TextEditor
    {
    public:
        explicit Editor (OutputCalibrationComponent& ownerToUse) : owner (ownerToUse) {}
        void focusGained (FocusChangeType cause) override;
    private:
        OutputCalibrationComponent& owner;
    };
    class Selector final : public juce::ComboBox
    {
    public:
        explicit Selector (OutputCalibrationComponent& ownerToUse) : owner (ownerToUse) {}
        void focusGained (FocusChangeType cause) override;
    private:
        OutputCalibrationComponent& owner;
    };
    class Button final : public juce::TextButton
    {
    public:
        explicit Button (OutputCalibrationComponent& ownerToUse) : owner (ownerToUse) {}
        void focusGained (FocusChangeType cause) override;
    private:
        OutputCalibrationComponent& owner;
    };
    juce::Viewport viewport;
    juce::Component content;
    Selector channel { *this }, signalKind { *this };
    Editor trim { *this }, delay { *this }, level { *this }, frequency { *this };
    Button polarity { *this }, startStop { *this };
    juce::Label channelLabel, trimLabel, delayLabel, signalLabel, levelLabel, frequencyLabel, status;
    juce::AudioChannelSet channels;
    std::vector<OutputChannelCalibration> settings;
    CalibrationTestSignal testSignal;
    CalibrationTestSignal::Kind acceptedSignalKind = CalibrationTestSignal::Kind::pinkNoise;
    float acceptedLevelDbFS = -30.0f;
    double acceptedFrequencyHz = 1000.0;
    bool refreshing = false;
};
