#pragma once

#include "library/MusicDirectory.h"
#include "skin/SkinComponentBindings.h"
#include "ui/JuceSkinAdapter.h"

#include <functional>
#include <memory>

class MusicDirectoriesPanel final
{
public:
    explicit MusicDirectoriesPanel (JuceSkinAdapter&);
    ~MusicDirectoriesPanel();
    void addTo (juce::Component&);
    void bind (SkinComponentBindings&, SkinValidationReport&);
    void refresh (const std::vector<MusicDirectory>&);
    void setScanState (bool running, const juce::String& status);
    void setVisible (bool);
    [[nodiscard]] juce::String selectedDirectoryId() const;

    std::function<void()> onAdd, onScan, onCancel;
    std::function<void (const juce::String&)> onRemove;
    std::function<void (const juce::String&, bool)> onAutomaticChanged;

private:
    class DirectoryList;
    void updateActions();
    std::vector<juce::Component*> controls();
    std::unique_ptr<DirectoryList> directories;
    juce::Label heading, status;
    juce::TextButton add { "Add folder" }, scan { "Scan now" }, cancel { "Cancel scan" }, remove { "Stop managing" };
    juce::ToggleButton automatic { "Automatic scanning" };
    bool scanning = false;
};
