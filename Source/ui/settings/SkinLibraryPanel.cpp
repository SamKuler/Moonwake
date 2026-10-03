#include "SkinLibraryPanel.h"

#include "SettingsSkinContext.h"

#include <array>
#include <algorithm>

class SkinLibraryPanel::SkinListView final : public juce::ListBox,
                                             private juce::ListBoxModel
{
public:
    explicit SkinListView (JuceSkinAdapter* adapterToUse)
        : juce::ListBox ("Installed skins", nullptr),
          adapter (adapterToUse)
    {
        setName ("Installed skins");
        setTitle ("Built-in and installed player skins");
        setWantsKeyboardFocus (true);
        setModel (this);
        setOutlineThickness (0);
        setRowHeight (1);
        setColour (juce::ListBox::backgroundColourId,
                   juce::Colour {});
        setColour (juce::ListBox::outlineColourId,
                   juce::Colour {});
    }

    ~SkinListView() override { setModel (nullptr); }

    void setSelections (std::vector<InstalledSkinDescription> selectionsToUse,
                        const SkinSelection& active, bool previewing)
    {
        const auto previous = selected();
        selections = std::move (selectionsToUse);
        activeSelection = active;
        previewActive = previewing;
        auto selectedIndex = indexOf (previous.value_or (active));
        if (selectedIndex < 0)
            selectedIndex = indexOf (active);
        if (selectedIndex < 0 && ! selections.empty())
            selectedIndex = 0;
        resized();
        updateContent();
        repaint();
        juce::SparseSet<int> selectedRows;
        if (selectedIndex >= 0)
            selectedRows.addRange ({ selectedIndex, selectedIndex + 1 });
        setSelectedRows (selectedRows, juce::dontSendNotification);
        ensureSelectionVisible();
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (
                juce::AccessibilityEvent::structureChanged);
    }

    [[nodiscard]] int count() const noexcept
    {
        return static_cast<int> (selections.size());
    }

    [[nodiscard]] int scrollPosition() const noexcept { return firstVisibleRow; }

    [[nodiscard]] std::optional<SkinSelection> selected() const
    {
        const auto selectedIndex = getSelectedRow();
        if (! juce::isPositiveAndBelow (selectedIndex,
                                        static_cast<int> (selections.size())))
            return std::nullopt;
        return selections[static_cast<size_t> (selectedIndex)].selection;
    }

    void setSelectionChanged (std::function<void()> callback)
    {
        selectionChanged = std::move (callback);
    }

    bool select (const SkinSelection& selection)
    {
        const auto index = indexOf (selection);
        if (index < 0)
            return false;
        setSelectedIndex (index, true);
        return true;
    }

    void paint (juce::Graphics& graphics) override
    {
        SkinRenderContext background;
        background.bounds = getLocalBounds().toFloat();
        background.contentBounds = background.bounds;
        background.state.enabled = isEnabled();
        background.state.focused = hasKeyboardFocus (true);
        if (adapter != nullptr)
            static_cast<void> (adapter->drawCustomComponent (
                graphics, *this, SkinRenderTarget::skinList,
                background, "settings.skins.library"));

        juce::ListBox::paint (graphics);
        if (selections.empty())
            if (const auto layout = rowLayout (getWidth()))
                drawTextSlot (graphics, *layout, "empty.message", "No skins available.", "empty");
    }

    void resized() override
    {
        const auto first = firstVisibleRow;
        const auto applyRows = [this] (int width)
        {
            if (const auto layout = rowLayout (width))
                setRowHeight (juce::jmax (1, juce::roundToInt (layout->rowHeight + layout->rowGap)));
        };
        juce::ListBox::resized();
        const auto width = getViewport()->getMaximumVisibleWidth();
        applyRows (width);
        juce::ListBox::resized();
        if (const auto availableWidth = getViewport()->getMaximumVisibleWidth(); availableWidth != width)
        {
            applyRows (availableWidth);
            juce::ListBox::resized();
        }
        setFirstVisibleRow (first);
        if (adapter != nullptr) adapter->configureListContentClip (*this, SkinRenderTarget::skinList);
    }

    void focusGained (juce::Component::FocusChangeType cause) override
    {
        juce::ListBox::focusGained (cause);
        repaint();
    }

    void focusLost (juce::Component::FocusChangeType cause) override
    {
        juce::ListBox::focusLost (cause);
        repaint();
    }

    void focusOfChildComponentChanged (
        juce::Component::FocusChangeType cause) override
    {
        juce::ListBox::focusOfChildComponentChanged (cause);
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (selections.empty()) return false;
        const auto selectedIndex = getSelectedRow();
        if (key == juce::KeyPress::upKey)
            return setSelectedIndex (selectedIndex - 1, true);
        if (key == juce::KeyPress::downKey)
            return setSelectedIndex (selectedIndex + 1, true);
        if (key == juce::KeyPress::pageUpKey)
            return setSelectedIndex (selectedIndex - visibleRowCount(), true);
        if (key == juce::KeyPress::pageDownKey)
            return setSelectedIndex (selectedIndex + visibleRowCount(), true);
        if (key == juce::KeyPress::homeKey)
            return setSelectedIndex (0, true);
        if (key == juce::KeyPress::endKey)
            return setSelectedIndex (static_cast<int> (selections.size()) - 1, true);
        return false;
    }

private:
    int getNumRows() override
    {
        return static_cast<int> (selections.size());
    }

    juce::String getNameForRow (int row) override
    {
        if (! juce::isPositiveAndBelow (row,
                                        static_cast<int> (selections.size())))
            return {};
        const auto& description = selections[static_cast<size_t> (row)];
        const auto& selection = description.selection;
        const auto active = selection == activeSelection;
        const auto status = active ? (previewActive ? ", previewing" : ", active")
                                   : juce::String();
        return displayName (description) + ", " + selection.id + ", version "
             + selection.version + ", "
             + (description.builtIn ? "built in" : "imported") + status;
    }

    void paintListBoxItem (int rowNumber, juce::Graphics& graphics,
                           int width, int height, bool rowIsSelected) override
    {
        if (! juce::isPositiveAndBelow (rowNumber,
                                        static_cast<int> (selections.size())))
            return;
        const auto layout = rowLayout (width);
        if (! layout) return;
        const auto& description = selections[static_cast<size_t> (rowNumber)];
        const auto& selection = description.selection;
        const auto builtIn = description.builtIn;
        const auto active = selection == activeSelection;
        const auto previewed = active && previewActive;
        SkinRenderContext rowContext;
        rowContext.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
        rowContext.contentBounds = rowContext.bounds;
        rowContext.state.enabled = true;
        rowContext.state.selected = rowIsSelected;
        rowContext.extraLookup = [rowNumber,
                                  id = selection.id, name = description.name,
                                  author = description.author,
                                  version = selection.version,
                                  builtIn, active, previewed]
            (std::string_view path) -> std::optional<SkinExpressionValue>
        {
            return SettingsSkinContext::skinListValue (
                path, rowNumber, id, name, author,
                version, builtIn, active, previewed);
        };
        const auto rowKey = "skin:" + juce::String (rowNumber);
        if (const auto surface = layout->slots.find ("row.surface"); surface != layout->slots.end())
        {
            auto surfaceContext = rowContext;
            surfaceContext.bounds = surface->second;
            surfaceContext.contentBounds = surfaceContext.bounds;
            static_cast<void> (adapter->drawCustomSlot (
                graphics, *this, SkinRenderTarget::skinList,
                "row.surface", SkinRenderTarget::surface,
                surfaceContext, rowKey));
        }
        drawTextSlot (graphics, *layout, "row.name", displayName (description),
                      rowKey, &rowContext);
        drawTextSlot (graphics, *layout, "row.author",
                      "by " + description.author,
                      rowKey, &rowContext);
        drawTextSlot (graphics, *layout, "row.version", "v" + selection.version,
                      rowKey, &rowContext);
        drawTextSlot (graphics, *layout, "row.status",
                      previewed ? "Preview"
                                : (active ? "Active"
                                          : (builtIn ? "Built in" : "Imported")),
                      rowKey,
                      &rowContext);
    }

    void selectedRowsChanged (int) override
    {
        if (selectionChanged)
            selectionChanged();
    }

    void listWasScrolled() override
    {
        if (const auto* viewport = getViewport())
            firstVisibleRow = juce::jmax (
                0, viewport->getViewPositionY() / getRowHeight());
    }

    [[nodiscard]] int visibleRowCount() const
    {
        return juce::jmax (1, getViewport()->getMaximumVisibleHeight() / getRowHeight());
    }

    bool setFirstVisibleRow (int requested)
    {
        const auto maximum = juce::jmax (
            0, static_cast<int> (selections.size()) - visibleRowCount());
        const auto next = juce::jlimit (0, maximum, requested);
        firstVisibleRow = next;
        if (auto* viewport = getViewport())
            viewport->setViewPosition (
                viewport->getViewPositionX(),
                firstVisibleRow * getRowHeight());
        return true;
    }

    bool setSelectedIndex (int requested, bool notify)
    {
        if (selections.empty()) return false;
        const auto next = juce::jlimit (
            0, static_cast<int> (selections.size()) - 1, requested);
        const auto changed = next != getSelectedRow();
        juce::SparseSet<int> selectedRows;
        selectedRows.addRange ({ next, next + 1 });
        setSelectedRows (selectedRows,
                         notify ? juce::sendNotification
                                : juce::dontSendNotification);
        ensureSelectionVisible();
        if (changed)
            repaintRow (next);
        return true;
    }

    void ensureSelectionVisible()
    {
        const auto selectedIndex = getSelectedRow();
        if (selectedIndex < 0)
            return;
        if (selectedIndex < firstVisibleRow)
            setFirstVisibleRow (selectedIndex);
        else if (selectedIndex >= firstVisibleRow + visibleRowCount())
            setFirstVisibleRow (selectedIndex - visibleRowCount() + 1);
    }

    [[nodiscard]] int indexOf (const SkinSelection& selection) const
    {
        const auto found = std::find_if (
            selections.begin(), selections.end(), [&selection] (const auto& item)
            {
                return item.selection == selection;
            });
        return found == selections.end()
                 ? -1 : static_cast<int> (std::distance (selections.begin(), found));
    }

    static juce::String displayName (const InstalledSkinDescription& description)
    {
        const auto& selection = description.selection;
        if (description.name.isNotEmpty()) return description.name;
        if (selection.id == "builtin.default") return "Listening B";
        if (selection.id == "builtin.foundation") return "Studio A Foundation";
        return selection.id;
    }

    [[nodiscard]] std::optional<SkinResolvedRowLayout> rowLayout (int width) const
    {
        if (adapter == nullptr) return std::nullopt;
        SkinRenderContext context;
        context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (getHeight()) };
        context.tableRowCount = static_cast<int> (selections.size());
        context.tableColumnCount = 4;
        return adapter->rowLayoutFor (*this, SkinRenderTarget::skinList, context);
    }

    void drawTextSlot (juce::Graphics& graphics, const SkinResolvedRowLayout& layout,
                       juce::StringRef slot, const juce::String& text,
                       const juce::String& key,
                       const SkinRenderContext* ownerContext = nullptr)
    {
        const auto frame = layout.slots.find (juce::String (slot));
        if (frame == layout.slots.end()) return;
        SkinRenderContext context;
        context.bounds = frame->second;
        context.contentBounds = context.bounds;
        context.primaryText = text;
        context.state.enabled = true;
        if (ownerContext != nullptr)
            context.extraLookup = ownerContext->extraLookup;
        if (adapter != nullptr)
            static_cast<void> (adapter->drawCustomSlot (
                graphics, *this, SkinRenderTarget::skinList,
                slot, SkinRenderTarget::label, context,
                key + ":" + juce::String (slot)));
    }

    JuceSkinAdapter* adapter = nullptr;
    std::vector<InstalledSkinDescription> selections;
    SkinSelection activeSelection;
    bool previewActive = false;
    int firstVisibleRow = 0;
    std::function<void()> selectionChanged;
};

SkinLibraryPanel::SkinLibraryPanel (SkinManager& managerToUse,
                                    JuceSkinAdapter* adapter)
    : manager (managerToUse),
      library (std::make_unique<SkinListView> (adapter))
{
    headingLabel.setText ("Appearance", juce::dontSendNotification);
    headingLabel.setName ("Skin settings");
    headingLabel.setTitle ("Appearance and skin settings");
    headingLabel.setJustificationType (juce::Justification::centredLeft);

    importButton.setButtonText ("Import skin");
    applyButton.setButtonText ("Apply");
    cancelPreviewButton.setButtonText ("Cancel preview");
    reloadButton.setButtonText ("Developer folder");
    importButton.setTitle ("Import a Moonwake skin package");
    removeButton.setTitle ("Remove the selected third-party skin version");
    previewButton.setTitle ("Preview the selected skin without saving it");
    applyButton.setTitle ("Apply and remember the previewed skin");
    cancelPreviewButton.setTitle ("Cancel the current skin preview");
    reloadButton.setTitle ("Choose or reload a developer skin directory");
    importButton.setTooltip ("Import a Moonwake skin package");
    removeButton.setTooltip ("Remove the selected third-party skin version");
    previewButton.setTooltip ("Preview the selected skin without saving it");
    applyButton.setTooltip ("Apply and remember the previewed skin");
    cancelPreviewButton.setTooltip ("Cancel the current skin preview");
    reloadButton.setTooltip ("Choose or reload a developer skin directory");

    library->setSelectionChanged ([this] { updateActions(); });
    importButton.onClick = [this]
    {
        if (importCallback)
            importCallback();
    };
    removeButton.onClick = [this] { removeSelected(); };
    previewButton.onClick = [this] { previewSelected(); };
    applyButton.onClick = [this] { applyPreview(); };
    cancelPreviewButton.onClick = [this] { cancelPreview(); };
    reloadButton.onClick = [this] { reloadDeveloperSkin(); };
    refresh();
}

SkinLibraryPanel::~SkinLibraryPanel() = default;

void SkinLibraryPanel::setStatusCallback (StatusCallback callback)
{
    statusCallback = std::move (callback);
}

void SkinLibraryPanel::setImportCallback (ActionCallback callback)
{
    importCallback = std::move (callback);
}

void SkinLibraryPanel::setChooseDeveloperDirectoryCallback (ActionCallback callback)
{
    chooseDeveloperDirectoryCallback = std::move (callback);
}

void SkinLibraryPanel::addTo (juce::Component& parent)
{
    for (auto* component : std::array<juce::Component*, 8> {
             &headingLabel, library.get(), &importButton, &removeButton, &previewButton,
             &applyButton, &cancelPreviewButton, &reloadButton })
        parent.addAndMakeVisible (component);
}

void SkinLibraryPanel::bind (SkinComponentBindings& bindings,
                             SkinValidationReport& report)
{
    static_cast<void> (bindings.bind ("settings.skins.heading", headingLabel, report));
    static_cast<void> (bindings.bind ("settings.skins.library", *library, report));
    static_cast<void> (bindings.bind ("settings.skins.import", importButton, report));
    static_cast<void> (bindings.bind ("settings.skins.remove", removeButton, report));
    static_cast<void> (bindings.bind ("settings.skins.preview", previewButton, report));
    static_cast<void> (bindings.bind ("settings.skins.apply", applyButton, report));
    static_cast<void> (bindings.bind ("settings.skins.cancelPreview", cancelPreviewButton, report));
    static_cast<void> (bindings.bind ("settings.skins.reload", reloadButton, report));
}

void SkinLibraryPanel::setVisible (bool shouldBeVisible)
{
    headingLabel.setVisible (shouldBeVisible);
    library->setVisible (shouldBeVisible);
    importButton.setVisible (shouldBeVisible);
    removeButton.setVisible (shouldBeVisible);
    previewButton.setVisible (shouldBeVisible);
    applyButton.setVisible (shouldBeVisible);
    cancelPreviewButton.setVisible (shouldBeVisible);
    reloadButton.setVisible (shouldBeVisible);
}

void SkinLibraryPanel::refresh()
{
    library->setSelections (manager.installedSkinDescriptions(),
                            manager.activeSelection(), manager.isPreviewing());
    reloadButton.setButtonText (
        manager.developerDirectory() == juce::File()
            ? "Developer folder" : "Reload folder");
    updateActions();
}

void SkinLibraryPanel::importFile (const juce::File& file)
{
    const auto reportValue = manager.importSkin (file);
    report (reportValue, "Skin imported. Select it to preview.");
    refresh();
}

void SkinLibraryPanel::setDeveloperDirectory (const juce::File& directory)
{
    SkinValidationReport reportValue;
    if (manager.setDeveloperDirectory (directory, reportValue))
    {
        if (manager.reloadDeveloperSkin (reportValue))
            report (reportValue, "Developer skin loaded in preview mode.");
    }
    if (reportValue.hasErrors())
        report (reportValue, {});
    refresh();
}

void SkinLibraryPanel::previewSelected()
{
    const auto selected = library->selected();
    if (! selected)
        return;
    SkinValidationReport reportValue;
    static_cast<void> (manager.preview (*selected, reportValue));
    report (reportValue, "Previewing " + selected->id + ".");
    refresh();
}

void SkinLibraryPanel::removeSelected()
{
    const auto selected = library->selected();
    if (! selected)
        return;
    SkinValidationReport reportValue;
    static_cast<void> (manager.remove (*selected, reportValue));
    report (reportValue, "Removed " + selected->id + " " + selected->version + ".");
    refresh();
}

void SkinLibraryPanel::applyPreview()
{
    SkinValidationReport reportValue;
    static_cast<void> (manager.applyPreview (reportValue));
    report (reportValue, "Skin applied.");
    refresh();
}

void SkinLibraryPanel::cancelPreview()
{
    manager.cancelPreview();
    if (statusCallback)
        statusCallback ("Skin preview cancelled.", false);
    refresh();
}

void SkinLibraryPanel::reloadDeveloperSkin()
{
    if (manager.developerDirectory() == juce::File())
    {
        if (chooseDeveloperDirectoryCallback)
            chooseDeveloperDirectoryCallback();
        return;
    }
    SkinValidationReport reportValue;
    static_cast<void> (manager.reloadDeveloperSkin (reportValue));
    report (reportValue, "Developer skin reloaded in preview mode.");
    refresh();
}

void SkinLibraryPanel::updateActions()
{
    const auto selected = library->selected();
    const auto builtIn = selected && selected->id.startsWith ("builtin.");
    const auto protectedSelection = selected
                                 && (*selected == manager.committedSelection()
                                     || *selected == manager.activeSelection());
    removeButton.setEnabled (selected.has_value() && ! builtIn && ! protectedSelection);
    previewButton.setEnabled (selected.has_value()
                              && *selected != manager.activeSelection());
    applyButton.setEnabled (manager.isPreviewing()
                            && ! manager.isDeveloperPreviewing());
    cancelPreviewButton.setEnabled (manager.isPreviewing());
}

void SkinLibraryPanel::report (const SkinValidationReport& reportValue,
                               const juce::String& successMessage)
{
    if (! statusCallback)
        return;
    if (reportValue.hasErrors())
        statusCallback (reportValue.toText(), true);
    else if (successMessage.isNotEmpty())
        statusCallback (successMessage, false);
}

juce::Component& SkinLibraryPanel::libraryControl() noexcept { return *library; }
juce::Label& SkinLibraryPanel::headingControl() noexcept { return headingLabel; }
juce::TextButton& SkinLibraryPanel::importControl() noexcept { return importButton; }
juce::TextButton& SkinLibraryPanel::removeControl() noexcept { return removeButton; }
juce::TextButton& SkinLibraryPanel::previewControl() noexcept { return previewButton; }
juce::TextButton& SkinLibraryPanel::applyControl() noexcept { return applyButton; }
juce::TextButton& SkinLibraryPanel::cancelPreviewControl() noexcept { return cancelPreviewButton; }
juce::TextButton& SkinLibraryPanel::reloadControl() noexcept { return reloadButton; }
int SkinLibraryPanel::skinCount() const noexcept { return library->count(); }
std::optional<SkinSelection> SkinLibraryPanel::selectedSkin() const { return library->selected(); }
bool SkinLibraryPanel::selectSkin (const SkinSelection& selection) { return library->select (selection); }
int SkinLibraryPanel::libraryScrollPosition() const noexcept { return library->scrollPosition(); }
