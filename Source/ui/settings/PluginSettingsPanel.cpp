#include "PluginSettingsPanel.h"

#include "SettingsSkinContext.h"

#include <array>

class PluginSettingsPanel::PluginListView final : public juce::ListBox,
                                                  private juce::ListBoxModel
{
public:
    explicit PluginListView (JuceSkinAdapter* adapterToUse)
        : juce::ListBox ("Known audio plug-ins", nullptr),
          adapter (adapterToUse)
    {
        setName ("Known audio plug-ins");
        setTitle ("Known audio plug-ins");
        setWantsKeyboardFocus (true);
        setModel (this);
        setOutlineThickness (0);
        setRowHeight (1);
        setColour (juce::ListBox::backgroundColourId,
                   juce::Colour {});
        setColour (juce::ListBox::outlineColourId,
                   juce::Colour {});
        auto header = std::make_unique<Header> (*this);
        header->setSize (1, 0);
        setHeaderComponent (std::move (header));
    }

    ~PluginListView() override { setModel (nullptr); }

    void setPlugins (juce::Array<juce::PluginDescription> pluginsToUse,
                     PluginFormatPreferences preferencesToUse, bool filtered)
    {
        preferences = preferencesToUse;
        emptyMessage = filtered ? "No matching plug-ins.\nChange the filters or search."
            : "No plug-ins found.\nEnable a format and scan to get started.";
        plugins = std::move (pluginsToUse);
        firstVisibleRow = 0;
        resized();
        updateContent();
        repaint();
        setFirstVisibleRow (0);
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (
                juce::AccessibilityEvent::structureChanged);
    }

    [[nodiscard]] int count() const noexcept { return plugins.size(); }
    [[nodiscard]] int scrollPosition() const noexcept { return firstVisibleRow; }

    void resized() override
    {
        const auto first = firstVisibleRow;
        const auto applyRows = [this] (int width)
        {
            if (const auto layout = rowLayout (width))
            {
                setRowHeight (juce::jmax (1, juce::roundToInt (layout->rowHeight + layout->rowGap)));
                if (auto* header = getHeaderComponent())
                    header->setSize (getWidth(), juce::roundToInt (layout->headerHeight));
            }
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
        if (adapter != nullptr)
            adapter->configureListContentClip (*this, SkinRenderTarget::pluginList);
    }

    void paint (juce::Graphics& graphics) override
    {
        SkinRenderContext background;
        background.bounds = getLocalBounds().toFloat();
        background.contentBounds = background.bounds;
        background.state.enabled = isEnabled();
        background.state.focused = JuceSkinAdapter::hasVisibleKeyboardFocus (*this, true);
        if (adapter != nullptr)
            static_cast<void> (adapter->drawCustomComponent (
                graphics, *this, SkinRenderTarget::pluginList,
                background, "settings.plugins.knownList"));
        juce::ListBox::paint (graphics);

        if (plugins.isEmpty())
            if (const auto layout = rowLayout (getWidth()))
                drawTextSlot (graphics, *layout, "empty.message",
                    emptyMessage, "empty");
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::upKey) return setFirstVisibleRow (firstVisibleRow - 1);
        if (key == juce::KeyPress::downKey) return setFirstVisibleRow (firstVisibleRow + 1);
        if (key == juce::KeyPress::pageUpKey)
            return setFirstVisibleRow (firstVisibleRow - visibleRowCount());
        if (key == juce::KeyPress::pageDownKey)
            return setFirstVisibleRow (firstVisibleRow + visibleRowCount());
        if (key == juce::KeyPress::homeKey) return setFirstVisibleRow (0);
        if (key == juce::KeyPress::endKey)
            return setFirstVisibleRow (plugins.size() - visibleRowCount());
        return false;
    }

private:
    class Header final : public juce::Component
    {
    public:
        explicit Header (PluginListView& ownerToUse) : owner (ownerToUse) {}
        void paint (juce::Graphics& graphics) override
        {
            if (const auto layout = owner.rowLayout (owner.getViewport()->getMaximumVisibleWidth()))
                owner.drawTextSlot (graphics, *layout, "header.label", "Known plug-ins", "heading");
        }

        std::unique_ptr<juce::AccessibilityHandler>
            createAccessibilityHandler() override
        {
            return juce::Component::createIgnoredAccessibilityHandler (*this);
        }

    private:
        PluginListView& owner;
    };

    int getNumRows() override { return plugins.size(); }

    juce::String getNameForRow (int row) override
    {
        if (! juce::isPositiveAndBelow (row, plugins.size()))
            return {};
        const auto& plugin = plugins.getReference (row);
        const auto manufacturer = plugin.manufacturerName.isNotEmpty()
                                    ? plugin.manufacturerName : "Unknown";
        return plugin.name + ", " + manufacturer + ", "
             + plugin.pluginFormatName + (preferences.allows (plugin.pluginFormatName) ? "" : ", format disabled");
    }

    void paintListBoxItem (int rowNumber, juce::Graphics& graphics,
                           int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (rowNumber, plugins.size()))
            return;
        const auto layout = rowLayout (width);
        if (! layout) return;
        const auto& plugin = plugins.getReference (rowNumber);
        const auto manufacturer = plugin.manufacturerName.isNotEmpty()
                                    ? plugin.manufacturerName
                                    : juce::String ("Unknown");
        SkinRenderContext rowContext;
        rowContext.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
        rowContext.contentBounds = rowContext.bounds;
        rowContext.state.enabled = preferences.allows (plugin.pluginFormatName);
        rowContext.extraLookup = [rowNumber,
                                  name = plugin.name, manufacturer,
                                  format = plugin.pluginFormatName, enabled = rowContext.state.enabled]
                                 (std::string_view path)
            -> std::optional<SkinExpressionValue>
        {
            return SettingsSkinContext::pluginListValue (
                path, rowNumber, name, manufacturer, format, enabled);
        };
        const auto rowKey = "plugin:" + juce::String (rowNumber);
        if (const auto surface = layout->slots.find ("row.surface"); surface != layout->slots.end())
        {
            auto surfaceContext = rowContext;
            surfaceContext.bounds = surface->second;
            surfaceContext.contentBounds = surfaceContext.bounds;
            static_cast<void> (adapter->drawCustomSlot (
                graphics, *this, SkinRenderTarget::pluginList,
                "row.surface", SkinRenderTarget::surface, surfaceContext, rowKey));
        }
        drawTextSlot (graphics, *layout, "row.name", plugin.name, rowKey,
                      &rowContext);
        drawTextSlot (graphics, *layout, "row.manufacturer",
                      manufacturer, rowKey, &rowContext);
        drawTextSlot (graphics, *layout, "row.format",
                      (plugin.pluginFormatName == "AudioUnit" ? "AU" : plugin.pluginFormatName)
                          + (rowContext.state.enabled ? "" : " (off)"), rowKey, &rowContext);
    }

    void listWasScrolled() override
    {
        if (auto* viewport = getViewport())
        {
            firstVisibleRow = juce::jmax (
                0, viewport->getViewPositionY() / getRowHeight());
        }
    }

    [[nodiscard]] int visibleRowCount() const
    {
        return juce::jmax (1, getViewport()->getMaximumVisibleHeight() / getRowHeight());
    }

    bool setFirstVisibleRow (int requested)
    {
        const auto maximum = juce::jmax (0, plugins.size() - visibleRowCount());
        const auto next = juce::jlimit (0, maximum, requested);
        firstVisibleRow = next;
        if (auto* viewport = getViewport())
            viewport->setViewPosition (
                viewport->getViewPositionX(),
                firstVisibleRow * getRowHeight());
        return true;
    }

    [[nodiscard]] std::optional<SkinResolvedRowLayout> rowLayout (int width) const
    {
        if (adapter == nullptr) return std::nullopt;
        SkinRenderContext context;
        context.bounds = { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (getHeight()) };
        context.tableRowCount = plugins.size();
        context.tableColumnCount = 3;
        return adapter->rowLayoutFor (*this, SkinRenderTarget::pluginList, context);
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
        {
            context.extraLookup = ownerContext->extraLookup;
            context.state.enabled = context.state.enabled && ownerContext->state.enabled;
        }
        if (adapter != nullptr)
            static_cast<void> (adapter->drawCustomSlot (
                graphics, *this, SkinRenderTarget::pluginList,
                slot, SkinRenderTarget::label, context,
                key + ":" + juce::String (slot)));
    }

    JuceSkinAdapter* adapter = nullptr;
    PluginFormatPreferences preferences;
    juce::String emptyMessage;
    juce::Array<juce::PluginDescription> plugins;
    int firstVisibleRow = 0;
};

PluginSettingsPanel::PluginSettingsPanel (PlayerController& controllerToUse,
                                          JuceSkinAdapter* adapter)
    : controller (controllerToUse),
      knownList (std::make_unique<PluginListView> (adapter))
{
    formatsLabel.setName ("Plug-in formats");
    formatsLabel.setTitle ("Supported plug-in formats");
    formatsLabel.setJustificationType (juce::Justification::centredLeft);
    vst3Enabled.setTitle ("Enable VST3 scanning and new effects");
    auEnabled.setTitle ("Enable Audio Unit scanning and new effects");
    for (auto* control : { &vst3Enabled, &auEnabled })
    {
        control->setTooltip ("Scan and add this plug-in format. Existing effects remain available.");
        control->onClick = [this] { changeFormats(); };
    }
    filters.onChange = [this] { refreshFilteredPlugins(); };
    scanAllButton.setButtonText ("Scan plug-ins");
    scanFolderButton.setButtonText ("Add folder");
    cancelButton.setButtonText ("Cancel scan");
    scanFolderButton.setTitle ("Add a custom VST3 scan folder");
    cancelButton.setTitle ("Cancel the active plug-in scan");
    const auto supportsAu = controller.getRegisteredPluginFormats().contains ("AudioUnit");
    scanFolderButton.setTooltip (supportsAu
        ? "Save a custom VST3 location for future scans. Audio Units use system registration."
        : "Save a custom VST3 location for future scans.");
    if (! supportsAu)
        auEnabled.setTooltip ("Audio Units are available on macOS only.");
    cancelButton.setTooltip ("Cancel the active plug-in scan");
    scanScope.setTitle ("Formats for this scan");
    scanScope.setTooltip ("Choose which enabled formats to scan. This does not change the display filter.");
    scanScope.addItem ("Enabled formats", 1);
    scanScope.addItem ("VST3 only", 2);
    if (supportsAu) scanScope.addItem ("AU only", 3);
    scanScope.setSelectedId (1, juce::dontSendNotification);
    scanScope.onChange = [this] { refreshScanState(); };
    defaultLocations.setTitle ("Default plug-in scan locations");
    defaultLocations.setJustificationType (juce::Justification::centredLeft);
    customDirectories.setTitle ("Custom VST3 scan folders");
    customDirectories.setTextWhenNothingSelected ("No custom VST3 folders");
    customDirectories.onChange = [this]
    {
        customDirectories.setTooltip (customDirectories.getText());
        refreshScanState();
    };
    removeDirectory.setTitle ("Remove the selected custom VST3 scan folder");
    removeDirectory.setTooltip ("Stop scanning this custom folder. Known plug-ins and loaded effects are kept.");
    removeDirectory.onClick = [this]
    {
        const auto index = customDirectories.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (index, customDirectoryPaths.size())) return;
        auto directories = customDirectoryPaths;
        directories.remove (index);
        if (! controller.setPluginCustomVst3Directories (std::move (directories)) && statusCallback)
            statusCallback (controller.getLastStatusMessage(), true);
        refreshDirectories();
        refreshScanState();
    };

    scanAllButton.onClick = [this]
    {
        if (! controller.startPluginScan ({}, selectedScanScope()) && statusCallback)
            statusCallback (controller.getLastStatusMessage(), true);
        refreshScanState();
    };
    scanFolderButton.onClick = [this]
    {
        if (scanFolderCallback)
            scanFolderCallback();
    };
    cancelButton.onClick = [this]
    {
        controller.cancelPluginScan();
        refreshScanState();
    };
    refreshInventory();
    refreshScanState();
}

PluginSettingsPanel::~PluginSettingsPanel() = default;

void PluginSettingsPanel::setStatusCallback (StatusCallback callback)
{
    statusCallback = std::move (callback);
}

void PluginSettingsPanel::setScanFolderCallback (ActionCallback callback)
{
    scanFolderCallback = std::move (callback);
}

void PluginSettingsPanel::addTo (juce::Component& parent)
{
    for (auto* component : std::array<juce::Component*, 12> {
             &formatsLabel, knownList.get(), &scanAllButton, &scanFolderButton,
             &cancelButton, &progressBar, &vst3Enabled, &auEnabled,
             &scanScope, &defaultLocations, &customDirectories, &removeDirectory })
        parent.addAndMakeVisible (component);
    filters.addTo (parent);
}

void PluginSettingsPanel::bind (SkinComponentBindings& bindings,
                                SkinValidationReport& report)
{
    static_cast<void> (bindings.bind ("settings.plugins.formats", formatsLabel, report));
    static_cast<void> (bindings.bind ("settings.plugins.knownList", *knownList, report));
    static_cast<void> (bindings.bind ("settings.plugins.scanAll", scanAllButton, report));
    static_cast<void> (bindings.bind ("settings.plugins.scanFolder", scanFolderButton, report));
    static_cast<void> (bindings.bind ("settings.plugins.cancel", cancelButton, report));
    static_cast<void> (bindings.bind ("settings.plugins.progress", progressBar, report));
    static_cast<void> (bindings.bind ("settings.plugins.enableVst3", vst3Enabled, report));
    static_cast<void> (bindings.bind ("settings.plugins.enableAu", auEnabled, report));
    static_cast<void> (bindings.bind ("settings.plugins.scanScope", scanScope, report));
    static_cast<void> (bindings.bind ("settings.plugins.defaultLocations", defaultLocations, report));
    static_cast<void> (bindings.bind ("settings.plugins.customDirectories", customDirectories, report));
    static_cast<void> (bindings.bind ("settings.plugins.removeDirectory", removeDirectory, report));
    filters.bind (bindings, "settings.plugins", report);
}

void PluginSettingsPanel::setVisible (bool shouldBeVisible)
{
    formatsLabel.setVisible (shouldBeVisible);
    knownList->setVisible (shouldBeVisible);
    scanAllButton.setVisible (shouldBeVisible);
    scanFolderButton.setVisible (shouldBeVisible);
    cancelButton.setVisible (shouldBeVisible);
    progressBar.setVisible (shouldBeVisible);
    for (auto* control : std::array<juce::Component*, 10> {
             &vst3Enabled, &auEnabled, &filters.search, &filters.type, &filters.manufacturer, &filters.format,
             &scanScope, &defaultLocations, &customDirectories, &removeDirectory })
        control->setVisible (shouldBeVisible);
}

void PluginSettingsPanel::refreshInventory()
{
    formatsLabel.setText ("Plug-ins", juce::dontSendNotification);
    const auto preferences = controller.getPluginFormatPreferences();
    vst3Enabled.setToggleState (preferences.vst3, juce::dontSendNotification);
    auEnabled.setToggleState (preferences.audioUnit, juce::dontSendNotification);
    inventory = controller.getKnownPlugins();
    filters.setPlugins (inventory);
    refreshFilteredPlugins();
    refreshDirectories();
}

void PluginSettingsPanel::refreshDirectories()
{
    const auto previousIndex = customDirectories.getSelectedId() - 1;
    const auto previous = juce::isPositiveAndBelow (previousIndex, customDirectoryPaths.size())
        ? customDirectoryPaths[previousIndex] : juce::String {};
    customDirectoryPaths = controller.getPluginCustomVst3Directories();
    customDirectories.clear (juce::dontSendNotification);
    for (int index = 0; index < customDirectoryPaths.size(); ++index)
    {
        const auto& path = customDirectoryPaths[index];
        customDirectories.addItem (path + (juce::File (path).isDirectory() ? "" : " (unavailable)"), index + 1);
    }
    const auto selected = customDirectoryPaths.indexOf (previous);
    customDirectories.setSelectedId (selected >= 0 ? selected + 1 : customDirectories.getItemId (0),
                                     juce::dontSendNotification);
    customDirectories.setTooltip (customDirectories.getText());
    const auto paths = controller.getDefaultVst3Directories();
    const auto supportsAu = controller.getRegisteredPluginFormats().contains ("AudioUnit");
    const auto text = "VST3 defaults: " + paths.joinIntoString ("; ")
                   + (supportsAu ? "\nAU: system-registered Audio Units" : "");
    defaultLocations.setText (supportsAu ? "Default VST3 locations / System Audio Units"
                                        : "Default VST3 locations", juce::dontSendNotification);
    defaultLocations.setTooltip (text);
}

PluginScanScope PluginSettingsPanel::selectedScanScope() const
{
    if (! scanScope.isVisible()) return PluginScanScope::enabledFormats;
    switch (scanScope.getSelectedId())
    {
        case 2: return PluginScanScope::vst3;
        case 3: return PluginScanScope::audioUnit;
        default: return PluginScanScope::enabledFormats;
    }
}

void PluginSettingsPanel::refreshFilteredPlugins()
{
    const auto filter = filters.filter();
    auto visible = inventory;
    visible.removeIf ([&] (const auto& plugin) { return ! filter.matches (plugin); });
    knownList->setPlugins (std::move (visible), controller.getPluginFormatPreferences(),
        filter.search.isNotEmpty() || filter.type.isNotEmpty() || filter.manufacturer.isNotEmpty() || filter.format.isNotEmpty());
}

void PluginSettingsPanel::changeFormats()
{
    if (! controller.setPluginFormatPreferences ({ vst3Enabled.getToggleState(), auEnabled.getToggleState() }) && statusCallback)
        statusCallback (controller.getLastStatusMessage(), true);
    refreshInventory();
    refreshScanState();
    if (onCatalogChanged) onCatalogChanged();
}

void PluginSettingsPanel::refreshScanState()
{
    const auto state = controller.getPluginScanStatus();
    progress = state.progress;
    progressBar.setTooltip (state.message);
    progressBar.repaint();
    const auto preferences = controller.getPluginFormatPreferences();
    const auto formats = controller.getRegisteredPluginFormats();
    vst3Enabled.setEnabled (! state.scanning && formats.contains ("VST3"));
    auEnabled.setEnabled (! state.scanning && formats.contains ("AudioUnit"));
    const auto scope = selectedScanScope();
    const auto scanVst3 = preferences.vst3 && formats.contains ("VST3") && scope != PluginScanScope::audioUnit;
    const auto scanAu = preferences.audioUnit && formats.contains ("AudioUnit") && scope != PluginScanScope::vst3;
    scanScope.setEnabled (! state.scanning);
    scanScope.setItemEnabled (2, formats.contains ("VST3"));
    scanScope.setItemEnabled (3, formats.contains ("AudioUnit"));
    scanAllButton.setEnabled (! state.scanning && (scanVst3 || scanAu));
    scanAllButton.setButtonText ("Scan");
    scanAllButton.setTitle (scope == PluginScanScope::vst3 ? "Scan VST3"
        : scope == PluginScanScope::audioUnit ? "Scan Audio Units" : "Scan enabled plug-in formats");
    scanAllButton.setTooltip (scanVst3 && scanAu ? "Scan VST3 defaults, custom folders and system-registered Audio Units."
        : scanVst3 ? "Scan VST3 defaults and custom folders."
        : scanAu ? "Scan system-registered Audio Units." : "Enable the selected plug-in format before scanning.");
    scanFolderButton.setEnabled (! state.scanning);
    customDirectories.setEnabled (! state.scanning && ! customDirectoryPaths.isEmpty());
    removeDirectory.setEnabled (! state.scanning && customDirectories.getSelectedId() > 0);
    cancelButton.setEnabled (state.scanning);

    if (state.revision != lastRevision)
    {
        lastRevision = state.revision;
        if (! state.scanning)
        {
            refreshInventory();
            if (onCatalogChanged) onCatalogChanged();
            if (statusCallback && state.message.isNotEmpty())
                statusCallback (state.message, state.failed > 0);
        }
    }
}

juce::Label& PluginSettingsPanel::formatsControl() noexcept { return formatsLabel; }
juce::Component& PluginSettingsPanel::knownListControl() noexcept { return *knownList; }
juce::TextButton& PluginSettingsPanel::scanAllControl() noexcept { return scanAllButton; }
juce::TextButton& PluginSettingsPanel::scanFolderControl() noexcept { return scanFolderButton; }
juce::TextButton& PluginSettingsPanel::cancelControl() noexcept { return cancelButton; }
juce::ProgressBar& PluginSettingsPanel::progressControl() noexcept { return progressBar; }
int PluginSettingsPanel::knownPluginCount() const noexcept { return knownList->count(); }
int PluginSettingsPanel::knownListScrollPosition() const noexcept { return knownList->scrollPosition(); }
