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
        setTitle ("Known VST3 and Audio Unit plug-ins");
        setWantsKeyboardFocus (true);
        setModel (this);
        setRowHeight (rowHeight);
        setColour (juce::ListBox::backgroundColourId,
                   juce::Colour {});
        setColour (juce::ListBox::outlineColourId,
                   juce::Colour {});
        auto header = std::make_unique<Header> (*this);
        header->setSize (1, headerHeight);
        setHeaderComponent (std::move (header));
    }

    ~PluginListView() override { setModel (nullptr); }

    void setPlugins (juce::Array<juce::PluginDescription> pluginsToUse)
    {
        plugins = std::move (pluginsToUse);
        firstVisibleRow = 0;
        updateContent();
        setFirstVisibleRow (0);
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (
                juce::AccessibilityEvent::structureChanged);
    }

    [[nodiscard]] int count() const noexcept { return plugins.size(); }
    [[nodiscard]] int scrollPosition() const noexcept { return firstVisibleRow; }

    void paint (juce::Graphics& graphics) override
    {
        SkinRenderContext background;
        background.bounds = getLocalBounds().toFloat();
        background.contentBounds = background.bounds;
        background.state.enabled = isEnabled();
        background.state.focused = hasKeyboardFocus (true);
        if (adapter != nullptr)
            static_cast<void> (adapter->drawCustomComponent (
                graphics, *this, SkinRenderTarget::pluginList,
                background, "settings.plugins.knownList"));
        juce::ListBox::paint (graphics);

        if (plugins.isEmpty())
        {
            auto emptyBounds = contentBounds();
            emptyBounds.removeFromTop (headerHeight);
            SkinRenderContext emptyContext;
            emptyContext.bounds = emptyBounds.toFloat().reduced (8.0f);
            emptyContext.contentBounds = emptyContext.bounds;
            emptyContext.primaryText = "No plug-ins discovered yet. Scan the standard VST3 and Audio Unit locations.";
            emptyContext.state.enabled = true;
            if (adapter != nullptr)
                static_cast<void> (adapter->drawCustomSlot (
                    graphics, *this, SkinRenderTarget::pluginList,
                    "row.status", SkinRenderTarget::label,
                    emptyContext, "empty"));
        }
    }

    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY == 0.0f)
            return;
        const auto step = wheel.deltaY > 0.0f ? -1 : 1;
        setFirstVisibleRow (firstVisibleRow + step);
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
            owner.drawTextSlot (graphics, getLocalBounds(), "row.name",
                                "KNOWN PLUG-INS", "heading");
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
             + plugin.pluginFormatName;
    }

    void paintListBoxItem (int rowNumber, juce::Graphics& graphics,
                           int width, int height, bool) override
    {
        if (! juce::isPositiveAndBelow (rowNumber, plugins.size()))
            return;
        auto row = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);
        const auto& plugin = plugins.getReference (rowNumber);
        const auto manufacturer = plugin.manufacturerName.isNotEmpty()
                                    ? plugin.manufacturerName
                                    : juce::String ("Unknown");
        SkinRenderContext rowContext;
        rowContext.bounds = row.toFloat();
        rowContext.contentBounds = rowContext.bounds;
        rowContext.state.enabled = true;
        rowContext.extraLookup = [rowNumber,
                                  name = plugin.name, manufacturer,
                                  format = plugin.pluginFormatName]
                                 (std::string_view path)
            -> std::optional<SkinExpressionValue>
        {
            return SettingsSkinContext::pluginListValue (
                path, rowNumber, name, manufacturer, format, true);
        };
        const auto rowKey = "plugin:" + juce::String (rowNumber);
        if (adapter != nullptr)
            static_cast<void> (adapter->drawCustomSlot (
                graphics, *this, SkinRenderTarget::pluginList,
                "row.surface", SkinRenderTarget::surface, rowContext, rowKey));

        auto formatBounds = row.removeFromRight (72);
        auto makerBounds = row.removeFromRight (
            juce::jmax (86, row.getWidth() / 3));
        drawTextSlot (graphics, row, "row.name", plugin.name, rowKey,
                      &rowContext);
        drawTextSlot (graphics, makerBounds, "row.manufacturer",
                      manufacturer, rowKey, &rowContext);
        drawTextSlot (graphics, formatBounds, "row.format",
                      plugin.pluginFormatName, rowKey, &rowContext);
    }

    void listWasScrolled() override
    {
        if (const auto* viewport = getViewport())
            firstVisibleRow = juce::jmax (
                0, viewport->getViewPositionY() / rowHeight);
    }

    [[nodiscard]] juce::Rectangle<int> contentBounds() const
    {
        return getLocalBounds().reduced (10, 8);
    }

    [[nodiscard]] int visibleRowCount() const
    {
        return juce::jmax (1, (contentBounds().getHeight() - headerHeight)
                                  / rowHeight);
    }

    bool setFirstVisibleRow (int requested)
    {
        const auto maximum = juce::jmax (0, plugins.size() - visibleRowCount());
        const auto next = juce::jlimit (0, maximum, requested);
        firstVisibleRow = next;
        if (auto* viewport = getViewport())
            viewport->setViewPosition (
                viewport->getViewPositionX(),
                firstVisibleRow * rowHeight);
        return true;
    }

    void drawTextSlot (juce::Graphics& graphics, juce::Rectangle<int> bounds,
                       juce::StringRef slot, const juce::String& text,
                       const juce::String& key,
                       const SkinRenderContext* ownerContext = nullptr)
    {
        SkinRenderContext context;
        context.bounds = bounds.toFloat();
        context.contentBounds = context.bounds;
        context.primaryText = text;
        context.state.enabled = true;
        if (ownerContext != nullptr)
            context.extraLookup = ownerContext->extraLookup;
        if (adapter != nullptr)
            static_cast<void> (adapter->drawCustomSlot (
                graphics, *this, SkinRenderTarget::pluginList,
                slot, SkinRenderTarget::label, context,
                key + ":" + juce::String (slot)));
    }

    JuceSkinAdapter* adapter = nullptr;
    juce::Array<juce::PluginDescription> plugins;
    int firstVisibleRow = 0;
    static constexpr int headerHeight = 28;
    static constexpr int rowHeight = 32;
};

PluginSettingsPanel::PluginSettingsPanel (PlayerController& controllerToUse,
                                          JuceSkinAdapter* adapter)
    : controller (controllerToUse),
      knownList (std::make_unique<PluginListView> (adapter))
{
    formatsLabel.setName ("Plug-in formats");
    formatsLabel.setTitle ("Supported plug-in formats");
    formatsLabel.setJustificationType (juce::Justification::centredLeft);
    scanAllButton.setTitle ("Scan standard VST3 and Audio Unit locations");
    scanFolderButton.setTitle ("Scan an additional VST3 folder");
    cancelButton.setTitle ("Cancel the active plug-in scan");
    scanAllButton.setTooltip ("Scan standard VST3 and Audio Unit locations");
    scanFolderButton.setTooltip ("Scan an additional VST3 folder");
    cancelButton.setTooltip ("Cancel the active plug-in scan");

    scanAllButton.onClick = [this]
    {
        if (! controller.startPluginScan() && statusCallback)
            statusCallback (controller.getLastStatusMessage());
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
    for (auto* component : std::array<juce::Component*, 6> {
             &formatsLabel, knownList.get(), &scanAllButton, &scanFolderButton,
             &cancelButton, &progressBar })
        parent.addAndMakeVisible (component);
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
}

void PluginSettingsPanel::setVisible (bool shouldBeVisible)
{
    formatsLabel.setVisible (shouldBeVisible);
    knownList->setVisible (shouldBeVisible);
    scanAllButton.setVisible (shouldBeVisible);
    scanFolderButton.setVisible (shouldBeVisible);
    cancelButton.setVisible (shouldBeVisible);
    progressBar.setVisible (shouldBeVisible);
}

void PluginSettingsPanel::refreshInventory()
{
    auto formats = controller.getRegisteredPluginFormats();
    for (auto& format : formats)
        if (format.containsIgnoreCase ("AudioUnit"))
            format = "AU";
    formatsLabel.setText (
        "PLUG-INS  /  " + (formats.isEmpty() ? juce::String ("VST3 + AU")
                                             : formats.joinIntoString (" + ")),
        juce::dontSendNotification);
    knownList->setPlugins (controller.getKnownPlugins());
}

bool PluginSettingsPanel::refreshScanState()
{
    const auto state = controller.getPluginScanStatus();
    progress = state.progress;
    progressBar.repaint();
    scanAllButton.setEnabled (! state.scanning);
    scanFolderButton.setEnabled (! state.scanning);
    cancelButton.setEnabled (state.scanning);

    const auto justFinished = wasScanning && ! state.scanning;
    wasScanning = state.scanning;
    if (state.revision != lastRevision)
    {
        lastRevision = state.revision;
        if (statusCallback && state.message.isNotEmpty())
            statusCallback (state.message);
    }
    if (justFinished)
        refreshInventory();
    return justFinished;
}

juce::Label& PluginSettingsPanel::formatsControl() noexcept { return formatsLabel; }
juce::Component& PluginSettingsPanel::knownListControl() noexcept { return *knownList; }
juce::TextButton& PluginSettingsPanel::scanAllControl() noexcept { return scanAllButton; }
juce::TextButton& PluginSettingsPanel::scanFolderControl() noexcept { return scanFolderButton; }
juce::TextButton& PluginSettingsPanel::cancelControl() noexcept { return cancelButton; }
juce::ProgressBar& PluginSettingsPanel::progressControl() noexcept { return progressBar; }
int PluginSettingsPanel::knownPluginCount() const noexcept { return knownList->count(); }
int PluginSettingsPanel::knownListScrollPosition() const noexcept { return knownList->scrollPosition(); }
