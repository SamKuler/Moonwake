#pragma once

#include "app/PlayerController.h"
#include "skin/SkinComponentBindings.h"
#include "ui/JuceSkinAdapter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

class AudioSettingsPanel final
{
public:
    using StatusCallback = std::function<void (const juce::String&, bool isError)>;

    AudioSettingsPanel (PlayerController&, JuceSkinAdapter* = nullptr);
    ~AudioSettingsPanel();

    void refresh();
    void setStatusCallback (StatusCallback);
    void addTo (juce::Component&);
    void bind (SkinComponentBindings&, SkinValidationReport&);
    void setVisible (bool);

    [[nodiscard]] juce::ComboBox& deviceControl() noexcept;
    [[nodiscard]] juce::ComboBox& sampleRateControl() noexcept;
    [[nodiscard]] juce::ComboBox& bufferSizeControl() noexcept;
    [[nodiscard]] juce::ComboBox& channelLayoutControl() noexcept;
    [[nodiscard]] juce::Label& headingControl() noexcept;
    [[nodiscard]] juce::Component& channelMapControl() noexcept;
    [[nodiscard]] int displayedChannelCount() const noexcept;

private:
    class ChannelMapView;

    void applyDeviceSelection();
    void applySampleRateSelection();
    void applyBufferSizeSelection();
    void applyChannelLayoutSelection();
    void reportStatus (const juce::String&, bool isError);

    PlayerController& controller;
    StatusCallback statusCallback;
    juce::Label headingLabel;
    juce::ComboBox deviceBox;
    juce::ComboBox sampleRateBox;
    juce::ComboBox bufferSizeBox;
    juce::ComboBox channelLayoutBox;
    std::unique_ptr<ChannelMapView> channelMap;
    juce::StringArray availableDevices;
    juce::Array<double> availableSampleRates;
    juce::Array<int> availableBufferSizes;
    bool refreshing = false;
};
