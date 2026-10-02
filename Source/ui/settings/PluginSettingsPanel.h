#pragma once

#include "app/PlayerController.h"
#include "skin/SkinComponentBindings.h"
#include "ui/JuceSkinAdapter.h"
#include "ui/PluginFilterControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

class PluginSettingsPanel final
{
public:
    using StatusCallback = std::function<void (const juce::String&, bool isError)>;
    using ActionCallback = std::function<void()>;

    PluginSettingsPanel (PlayerController&, JuceSkinAdapter* = nullptr);
    ~PluginSettingsPanel();

    void refreshInventory();
    bool refreshScanState();
    void setStatusCallback (StatusCallback);
    void setScanFolderCallback (ActionCallback);
    void addTo (juce::Component&);
    void bind (SkinComponentBindings&, SkinValidationReport&);
    void setVisible (bool);

    [[nodiscard]] juce::Label& formatsControl() noexcept;
    [[nodiscard]] juce::Component& knownListControl() noexcept;
    [[nodiscard]] juce::TextButton& scanAllControl() noexcept;
    [[nodiscard]] juce::TextButton& scanFolderControl() noexcept;
    [[nodiscard]] juce::TextButton& cancelControl() noexcept;
    [[nodiscard]] juce::ProgressBar& progressControl() noexcept;
    [[nodiscard]] int knownPluginCount() const noexcept;
    [[nodiscard]] int knownListScrollPosition() const noexcept;
    PluginFilterControls filters;
    juce::ToggleButton vst3Enabled { "VST3" }, auEnabled { "AU" };
    ActionCallback onFormatsChanged;

private:
    class PluginListView;

    PlayerController& controller;
    StatusCallback statusCallback;
    ActionCallback scanFolderCallback;
    juce::Label formatsLabel;
    std::unique_ptr<PluginListView> knownList;
    juce::TextButton scanAllButton { "Scan VST3 + AU" };
    juce::TextButton scanFolderButton { "Add VST3 Folder" };
    juce::TextButton cancelButton { "Cancel Scan" };
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };
    bool wasScanning = false;
    uint64_t lastRevision = 0;
    juce::Array<juce::PluginDescription> inventory;
    void refreshFilteredPlugins();
    void changeFormats();
};
