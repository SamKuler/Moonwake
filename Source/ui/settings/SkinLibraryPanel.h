#pragma once

#include "skin/SkinComponentBindings.h"
#include "skin/SkinManager.h"
#include "ui/JuceSkinAdapter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

class SkinLibraryPanel final
{
public:
    using StatusCallback = std::function<void (const juce::String&, bool isError)>;
    using ActionCallback = std::function<void()>;

    SkinLibraryPanel (SkinManager&, JuceSkinAdapter* = nullptr);
    ~SkinLibraryPanel();

    void refresh();
    void importFile (const juce::File&);
    void setDeveloperDirectory (const juce::File&);
    void setStatusCallback (StatusCallback);
    void setImportCallback (ActionCallback);
    void setChooseDeveloperDirectoryCallback (ActionCallback);
    void addTo (juce::Component&);
    void bind (SkinComponentBindings&, SkinValidationReport&);
    void setVisible (bool);

    [[nodiscard]] juce::Component& libraryControl() noexcept;
    [[nodiscard]] juce::Label& headingControl() noexcept;
    [[nodiscard]] juce::TextButton& importControl() noexcept;
    [[nodiscard]] juce::TextButton& removeControl() noexcept;
    [[nodiscard]] juce::TextButton& previewControl() noexcept;
    [[nodiscard]] juce::TextButton& applyControl() noexcept;
    [[nodiscard]] juce::TextButton& cancelPreviewControl() noexcept;
    [[nodiscard]] juce::TextButton& reloadControl() noexcept;
    [[nodiscard]] int skinCount() const noexcept;
    [[nodiscard]] std::optional<SkinSelection> selectedSkin() const;
    bool selectSkin (const SkinSelection&);
    [[nodiscard]] int libraryScrollPosition() const noexcept;

private:
    class SkinListView;

    void previewSelected();
    void removeSelected();
    void applyPreview();
    void cancelPreview();
    void reloadDeveloperSkin();
    void updateActions();
    void report (const SkinValidationReport&, const juce::String& successMessage);

    SkinManager& manager;
    StatusCallback statusCallback;
    ActionCallback importCallback;
    ActionCallback chooseDeveloperDirectoryCallback;
    juce::Label headingLabel;
    std::unique_ptr<SkinListView> library;
    juce::TextButton importButton { "Import Skin" };
    juce::TextButton removeButton { "Remove" };
    juce::TextButton previewButton { "Preview" };
    juce::TextButton applyButton { "Apply Preview" };
    juce::TextButton cancelPreviewButton { "Cancel Preview" };
    juce::TextButton reloadButton { "Developer Skin..." };
};
