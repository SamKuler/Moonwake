#include "MusicDirectoriesPanel.h"

#include "SettingsSkinContext.h"

#include <algorithm>

class MusicDirectoriesPanel::DirectoryList final : public juce::ListBox,
                                                   private juce::ListBoxModel
{
public:
    explicit DirectoryList (JuceSkinAdapter& skin) : juce::ListBox ("Music folders"), adapter (skin)
    {
        setModel (this);
        setName ("Managed music folders");
        setWantsKeyboardFocus (true);
        adapter.configureListBox (*this);
        setRowHeight (1);
    }
    ~DirectoryList() override { setModel (nullptr); }
    std::function<void()> onSelection;
    std::vector<MusicDirectory> items;

    const MusicDirectory* selected() const
    {
        const auto index = getSelectedRow();
        return juce::isPositiveAndBelow (index, static_cast<int> (items.size()))
            ? &items[static_cast<size_t> (index)] : nullptr;
    }

    void refresh (const std::vector<MusicDirectory>& next)
    {
        const auto oldId = selected() != nullptr ? selected()->id : juce::String();
        const auto scroll = getViewport()->getViewPosition();
        items = next;
        updateContent();
        auto found = std::find_if (items.begin(), items.end(), [&] (const auto& item) { return item.id == oldId; });
        selectRow (found == items.end() ? (items.empty() ? -1 : 0)
                                     : static_cast<int> (std::distance (items.begin(), found)));
        resized();
        getViewport()->setViewPosition (scroll);
        repaint();
    }

    void paint (juce::Graphics& graphics) override
    {
        SkinRenderContext context;
        context.bounds = context.contentBounds = getLocalBounds().toFloat();
        context.state.enabled = isEnabled();
        context.state.focused = JuceSkinAdapter::hasVisibleKeyboardFocus (*this, true);
        adapter.drawCustomComponent (graphics, *this, SkinRenderTarget::directoryList, context);
        juce::ListBox::paint (graphics);
        if (items.empty())
            if (const auto layout = rowLayout (getWidth()))
                drawSlot (graphics, *layout, "empty.message", "Add a folder to keep your music library up to date.", context);
    }
    void resized() override
    {
        juce::ListBox::resized();
        if (const auto layout = rowLayout (getViewport()->getMaximumVisibleWidth()))
            setRowHeight (juce::jmax (1, juce::roundToInt (layout->rowHeight + layout->rowGap)));
        juce::ListBox::resized();
        adapter.configureListContentClip (*this, SkinRenderTarget::directoryList);
    }
    void focusGained (juce::Component::FocusChangeType cause) override { juce::ListBox::focusGained (cause); repaint(); }
    void focusLost (juce::Component::FocusChangeType cause) override { juce::ListBox::focusLost (cause); repaint(); }

private:
    int getNumRows() override { return static_cast<int> (items.size()); }
    void selectedRowsChanged (int) override { if (onSelection) onSelection(); }
    juce::String getNameForRow (int index) override
    {
        if (! juce::isPositiveAndBelow (index, getNumRows())) return {};
        const auto& item = items[static_cast<size_t> (index)];
        return item.folder.getFullPathName() + (item.automatic ? ", automatic scanning" : ", scanning paused");
    }
    std::optional<SkinResolvedRowLayout> rowLayout (int width) const
    {
        SkinRenderContext context;
        context.bounds = { 0, 0, static_cast<float> (width), static_cast<float> (getHeight()) };
        context.tableRowCount = static_cast<int> (items.size());
        return adapter.rowLayoutFor (*this, SkinRenderTarget::directoryList, context);
    }
    void drawSlot (juce::Graphics& graphics, const SkinResolvedRowLayout& layout,
                   const juce::String& slot, const juce::String& text, SkinRenderContext context)
    {
        const auto found = layout.slots.find (slot);
        if (found == layout.slots.end()) return;
        context.bounds = context.contentBounds = found->second;
        context.primaryText = text;
        adapter.drawCustomSlot (graphics, *this, SkinRenderTarget::directoryList, slot,
                                slot == "row.surface" ? SkinRenderTarget::surface : SkinRenderTarget::label,
                                context, text + slot);
    }
    void paintListBoxItem (int index, juce::Graphics& graphics, int width, int height, bool selectedRow) override
    {
        if (! juce::isPositiveAndBelow (index, getNumRows())) return;
        const auto layout = rowLayout (width);
        if (! layout) return;
        const auto& item = items[static_cast<size_t> (index)];
        SkinRenderContext context;
        context.bounds = context.contentBounds = juce::Rectangle<float> (0, 0, static_cast<float> (width), static_cast<float> (height));
        context.state.enabled = isEnabled();
        context.state.selected = selectedRow;
        context.extraLookup = [index, item] (std::string_view path)
        {
            return SettingsSkinContext::directoryListValue (path, index, item.id,
                item.folder.getFileName(), item.folder.getFullPathName(), item.automatic);
        };
        drawSlot (graphics, *layout, "row.surface", {}, context);
        drawSlot (graphics, *layout, "row.name", item.folder.getFileName(), context);
        drawSlot (graphics, *layout, "row.path", item.folder.getFullPathName(), context);
        drawSlot (graphics, *layout, "row.status", item.automatic ? "Automatic" : "Paused", context);
    }
    JuceSkinAdapter& adapter;
};

MusicDirectoriesPanel::MusicDirectoriesPanel (JuceSkinAdapter& adapter)
    : directories (std::make_unique<DirectoryList> (adapter))
{
    heading.setText ("Music library", juce::dontSendNotification);
    heading.setName ("Music library settings");
    status.setName ("Music folder scan status");
    remove.setTooltip ("Stop watching the selected folder; keep its tracks and playlists");
    automatic.setTooltip ("Scan the selected folder on startup and for later changes");
    for (auto* component : controls()) component->setLookAndFeel (&adapter);
    directories->onSelection = [this] { updateActions(); };
    add.onClick = [this] { if (onAdd) onAdd(); };
    scan.onClick = [this] { if (onScan) onScan(); };
    cancel.onClick = [this] { if (onCancel) onCancel(); };
    remove.onClick = [this] { if (onRemove && selectedDirectoryId().isNotEmpty()) onRemove (selectedDirectoryId()); };
    automatic.onClick = [this]
    {
        if (onAutomaticChanged && selectedDirectoryId().isNotEmpty())
            onAutomaticChanged (selectedDirectoryId(), automatic.getToggleState());
    };
    updateActions();
}
MusicDirectoriesPanel::~MusicDirectoriesPanel() { for (auto* component : controls()) component->setLookAndFeel (nullptr); }
std::vector<juce::Component*> MusicDirectoriesPanel::controls()
{
    return { &heading, directories.get(), &add, &scan, &cancel, &remove, &automatic, &status };
}
void MusicDirectoriesPanel::addTo (juce::Component& parent) { for (auto* component : controls()) parent.addAndMakeVisible (component); }
void MusicDirectoriesPanel::bind (SkinComponentBindings& bindings, SkinValidationReport& report)
{
    const juce::StringArray ids { "heading", "directories", "add", "scan", "cancel", "remove", "automatic", "status" };
    const auto components = controls();
    for (int index = 0; index < ids.size(); ++index)
        bindings.bind ("settings.music." + ids[index], *components[static_cast<size_t> (index)], report);
}
void MusicDirectoriesPanel::refresh (const std::vector<MusicDirectory>& items) { directories->refresh (items); updateActions(); }
void MusicDirectoriesPanel::setVisible (bool visible) { for (auto* component : controls()) component->setVisible (visible); }
void MusicDirectoriesPanel::setScanState (bool running, const juce::String& message)
{
    scanning = running;
    status.setText (message, juce::dontSendNotification);
    updateActions();
}
juce::String MusicDirectoriesPanel::selectedDirectoryId() const
{
    const auto* item = directories->selected();
    return item != nullptr ? item->id : juce::String();
}
void MusicDirectoriesPanel::updateActions()
{
    const auto* item = directories->selected();
    add.setEnabled (! scanning);
    scan.setEnabled (! scanning && ! directories->items.empty());
    cancel.setEnabled (scanning);
    remove.setEnabled (! scanning && item != nullptr);
    automatic.setEnabled (! scanning && item != nullptr);
    automatic.setToggleState (item != nullptr && item->automatic, juce::dontSendNotification);
}
