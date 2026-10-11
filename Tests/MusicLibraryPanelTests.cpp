#include "fixtures/TestComponentPeer.h"
#include "ui/MusicLibraryPanel.h"
#include "ui/SkinEffectChainViewport.h"
#include "ui/SkinInputScope.h"
#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"

#include <algorithm>

namespace
{
class LibraryPanelAudioBackend final : public IAudioDeviceBackend,
                                       private juce::AudioIODevice
{
public:
    LibraryPanelAudioBackend() : AudioIODevice ("Library panel test", "Test")
    {
        setup.outputDeviceName = getName();
        setup.sampleRate = 48000.0;
        setup.bufferSize = 256;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.setRange (0, 2, true);
    }
    juce::String initialiseWithDefaultDevices (int, int) override { return {}; }
    juce::StringArray getOutputDeviceNames() const override { return { getName() }; }
    AudioDeviceBackendState getCurrentDeviceState() const override
    {
        return { true, getName(), 48000.0, { 48000.0 }, 256, { 256 }, 2, {}, setup.outputChannels };
    }
    juce::AudioDeviceManager::AudioDeviceSetup getAudioDeviceSetup() const override { return setup; }
    juce::String setAudioDeviceSetup (const juce::AudioDeviceManager::AudioDeviceSetup& value, bool) override
    {
        setup = value;
        return {};
    }
    void addAudioCallback (juce::AudioIODeviceCallback* callback) override { callback->audioDeviceAboutToStart (this); }
    void removeAudioCallback (juce::AudioIODeviceCallback* callback) override { callback->audioDeviceStopped(); }

private:
    juce::StringArray getOutputChannelNames() override { return { "Left", "Right" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 256 }; }
    int getDefaultBufferSize() override { return 256; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 256; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return setup.outputChannels; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
    juce::AudioDeviceManager::AudioDeviceSetup setup;
};

struct LibraryPanelSkin
{
    LibraryPanelSkin() : adapter ([this] (const juce::Component&, SkinRenderTarget target, const juce::String& style)
    {
        return JuceSkinAdapter::RuntimeBinding { compiled ? compiled->resolveStyle (target, style) : nullptr };
    }, renderer)
    {
        const auto foundationPackage = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, report);
        const auto foundation = foundationPackage ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
        const auto package = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::defaultSkin, report);
        if (package && foundation) compiled = SkinCompiler::compile (*package, foundation, report);
    }
    SkinValidationReport report;
    std::shared_ptr<const CompiledSkin> compiled;
    SkinRenderer renderer;
    JuceSkinAdapter adapter;
};

struct LibraryPanelFixture
{
    LibraryPanelFixture (LibraryPanelSkin& skin, const juce::File& root,
                         const std::vector<TrackMetadataResult>& prepared)
        : controller (std::make_unique<LibraryPanelAudioBackend>(), {}, root.getChildFile ("artwork")),
          panel (controller, skin.adapter)
    {
        host.setLookAndFeel (&skin.adapter);
        host.setBounds (0, 0, 640, 480);
        panel.addTo (host);
        panel.bind (bindings, report);
        table().setBounds (0, 80, 640, 400);
        table().getProperties().set (JuceSkinAdapter::styleProperty, "table.studio");
        panel.collectionsControl().setBounds (0, 0, 400, 36);
        bindings.find ("library.actions")->setBounds (420, 0, 100, 36);
        bindings.find ("library.newPlaylist")->setBounds (528, 0, 110, 36);
        controller.importPreparedTracks (prepared, "all");
        for (const auto& track : controller.getLibrary().getTracks()) ids.push_back (track.id);
        playlistId = controller.createSavedPlaylist ("First playlist");
        controller.addTracksToSavedPlaylist (playlistId, { ids[0], ids[1], ids[0], ids[2] });
        panel.selectCollection ("playlist:" + playlistId);
        host.addToDesktop (0);
        host.setVisible (true);
    }
    ~LibraryPanelFixture()
    {
        juce::PopupMenu::dismissAllActiveMenus();
        waitUntil ([this] { return activeMenu == nullptr; });
        host.setLookAndFeel (nullptr);
    }
    juce::TableListBox& table() { return dynamic_cast<juce::TableListBox&> (panel.tableControl()); }
    juce::TableListBoxModel& model() { return *table().getTableListBoxModel(); }
    void select (std::initializer_list<int> rows)
    {
        juce::SparseSet<int> selected;
        for (auto row : rows) selected.addRange ({ row, row + 1 });
        table().setSelectedRows (selected);
    }
    juce::Component::SafePointer<juce::Component> openActions()
    {
        dynamic_cast<juce::TextButton*> (bindings.find ("library.actions"))->onClick();
        activeMenu = juce::Component::getCurrentlyModalComponent();
        return activeMenu;
    }
    juce::Component::SafePointer<juce::Component> openDetailMenu (const juce::String& componentId)
    {
        if (! activateMenuHost()) return {};
        bindings.find ("library.detailCover")->setBounds (0, 40, 48, 32);
        bindings.find ("library.title")->setBounds (60, 40, 340, 32);
        panel.updateVisibility();
        auto* component = bindings.find (componentId);
        const auto point = host.getLocalPoint (component, component->getLocalBounds().getCentre()).toFloat();
        if (! waitUntil ([&] { return host.getComponentAt (point) == component; })) return {};
        auto* peer = host.getPeer();
        const auto time = juce::Time::currentTimeMillis();
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, {}, 0, 0, time);
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point,
                               juce::ModifierKeys::rightButtonModifier, 0, 0, time + 1);
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, {}, 0, 0, time + 2);
        waitUntil ([] { return juce::Component::getCurrentlyModalComponent() != nullptr; });
        activeMenu = juce::Component::getCurrentlyModalComponent();
        return activeMenu;
    }
    static juce::StringArray menuTitles (juce::Component& menu)
    {
        juce::StringArray result;
        const auto collect = [&] (auto&& self, juce::Component& component) -> void
        {
            if (auto* accessibility = component.getAccessibilityHandler()) result.add (accessibility->getTitle());
            for (auto* child : component.getChildren()) self (self, *child);
        };
        collect (collect, menu);
        return result;
    }
    bool chooseAction (juce::Component::SafePointer<juce::Component> menu, int action)
    {
        if (menu == nullptr) return false;
        auto result = std::make_shared<std::optional<int>>();
        juce::ModalComponentManager::getInstance()->attachCallback (menu.getComponent(),
            juce::ModalCallbackFunction::create ([result] (int value) { *result = value; }));
        menu->exitModalState (action);
        return waitUntil ([&] { return result->has_value() && menu == nullptr; }) && **result == action;
    }
    juce::Colour markerPixel (int row)
    {
        juce::Image image (juce::Image::ARGB, 400, 40, true);
        juce::Graphics graphics (image);
        model().paintRowBackground (graphics, row, 400, 40, table().isRowSelected (row));
        return image.getPixelAt (2, 20);
    }
    const std::vector<PlaylistEntry>& entries() const { return controller.getLibrary().getPlaylist (playlistId)->entries; }

    juce::TextEditor* renameEditor() const
    {
        for (auto* child : bindings.find ("library.collections")->getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child)) return label->getCurrentTextEditor();
        return nullptr;
    }
    bool clickNewPlaylist()
    {
        if (! activateMenuHost()) return false;
        auto* button = bindings.find ("library.newPlaylist");
        const auto point = host.getLocalPoint (button, button->getLocalBounds().getCentre()).toFloat();
        auto* peer = host.getPeer();
        if (! waitUntil ([&] { return host.contains (point) && host.getComponentAt (point) == button; })) return false;
        const auto time = juce::Time::currentTimeMillis();
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, {}, 0.0f, 0.0f, time);
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point,
                               juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, time + 1);
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, {}, 0.0f, 0.0f, time + 2);
        activeMenu = juce::Component::getCurrentlyModalComponent();
        return chooseAction (activeMenu, 20);
    }

    static bool activateMenuHost()
    {
       #if JUCE_MAC
        // Desktop focus can change between tests; PopupMenu observes it on mouse-up.
        juce::Process::makeForegroundProcess();
        return waitUntil ([] { return juce::Process::isForegroundProcess(); });
       #else
        return true;
       #endif
    }

    static bool waitUntil (const std::function<bool()>& complete)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1500.0;
        while (! complete() && juce::Time::getMillisecondCounterHiRes() < deadline)
            if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (10)) break;
        return complete();
    }

    PlayerController controller;
    TestUiRoot host;
    MusicLibraryPanel panel;
    SkinComponentBindings bindings;
    SkinValidationReport report;
    std::vector<juce::String> ids;
    juce::String playlistId;
    juce::Component::SafePointer<juce::Component> activeMenu;
};

class MusicLibraryPanelTests final : public juce::UnitTest
{
public:
    MusicLibraryPanelTests() : UnitTest ("MusicLibraryPanel", "ui") {}

    void runTest() override
    {
        beginTest ("create isolated audio and skin fixtures");
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("moonwake-library-panel-" + juce::Uuid().toString());
        expect (root.createDirectory());
        std::vector<TrackMetadataResult> prepared;
        for (const auto* title : { "Zulu", "Alpha", "Middle" })
        {
            const auto file = root.getChildFile (juce::String (title) + ".wav");
            {
                std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
                juce::WavAudioFormat wav;
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                    .withSampleRate (48000).withNumChannels (2).withBitsPerSample (16));
                expect (writer != nullptr);
                if (! writer) continue;
                juce::AudioBuffer<float> samples (2, 48000);
                samples.clear();
                expect (writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()));
            }
            prepared.push_back (TrackMetadataReader().read (file));
            expect (prepared.back().ok, prepared.back().error);
            prepared.back().track.title = title;
        }
        LibraryPanelSkin skin;
        expect (skin.compiled != nullptr, skin.report.toText());
        if (prepared.size() != 3 || skin.compiled == nullptr
            || std::any_of (prepared.begin(), prepared.end(), [] (const auto& metadata) { return ! metadata.ok; }))
        {
            root.deleteRecursively();
            return;
        }

        beginTest ("consumed arrow and text keys restore focus feedback after mouse input");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            SkinInputScope scope (fixture.host);
            const auto click = [&] (juce::Component& component, juce::Point<int> local)
            {
                const auto point = fixture.host.getLocalPoint (&component, local).toFloat();
                expect (LibraryPanelFixture::waitUntil ([&] { return fixture.host.contains (point); }));
                auto* peer = fixture.host.getPeer();
                const auto time = juce::Time::currentTimeMillis();
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, {}, 0, 0, time);
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point,
                                       juce::ModifierKeys::leftButtonModifier, 0, 0, time + 1);
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, point, {}, 0, 0, time + 2);
            };
            click (fixture.table(), fixture.table().getRowPosition (0, true).getCentre());
            expect (fixture.table().hasKeyboardFocus (true));
            expect (! JuceSkinAdapter::hasVisibleKeyboardFocus (fixture.table(), true));
            fixture.host.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::downKey));
            expectEquals (fixture.table().getSelectedRow(), 1);
            expect (JuceSkinAdapter::hasVisibleKeyboardFocus (fixture.table(), true));
            auto editor = std::make_unique<juce::TextEditor>();
            editor->setBounds (0, 0, 200, 36);
            fixture.host.addAndMakeVisible (*editor);
            click (*editor, { 40, 18 });
            expect (! JuceSkinAdapter::hasVisibleKeyboardFocus (*editor));
            fixture.host.getPeer()->handleKeyPress (juce::KeyPress ('x', {}, 'x'));
            expectEquals (editor->getText(), juce::String ("x"));
            expect (JuceSkinAdapter::hasVisibleKeyboardFocus (*editor));
            editor.reset();
            fixture.table().grabKeyboardFocus();
            expect (LibraryPanelFixture::waitUntil ([&] { return fixture.table().hasKeyboardFocus (true); }));
            click (fixture.table(), fixture.table().getRowPosition (0, true).getCentre());
            fixture.host.getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::downKey));
            expect (JuceSkinAdapter::hasVisibleKeyboardFocus (fixture.table(), true));
        }

        beginTest ("mouse focus does not tint buttons or empty lists but keyboard focus stays visible");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            fixture.controller.createSavedPlaylist ("Empty");
            fixture.panel.selectCollection ("favourites");
            SkinEffectChainViewport effects;
            effects.getProperties().set (JuceSkinAdapter::styleProperty, "effectChain.studio");
            effects.setBounds (0, 80, 640, 300);
            effects.setWantsKeyboardFocus (true);
            fixture.host.addChildComponent (effects);
            juce::Component focusSink;
            focusSink.setBounds (0, 0, 1, 1);
            focusSink.setWantsKeyboardFocus (true);
            fixture.host.addAndMakeVisible (focusSink);
            for (auto* component : { fixture.bindings.find ("library.actions"),
                                     &fixture.panel.tableControl(), static_cast<juce::Component*> (&effects) })
            {
                component->setVisible (true);
                focusSink.grabKeyboardFocus();
                fixture.host.getProperties().set (JuceSkinAdapter::focusVisibleProperty, false);
                const auto point = juce::Point<int> (4, 10);
                const auto normal = component->createComponentSnapshot (component->getLocalBounds()).getPixelAt (point.x, point.y);
                component->grabKeyboardFocus();
                expect (component->hasKeyboardFocus (true));
                expect (! JuceSkinAdapter::hasVisibleKeyboardFocus (*component, true));
                const auto clicked = component->createComponentSnapshot (component->getLocalBounds()).getPixelAt (point.x, point.y);
                expect (normal == clicked, "Mouse focus preserves the normal surface");
                fixture.host.getProperties().set (JuceSkinAdapter::focusVisibleProperty, true);
                expect (JuceSkinAdapter::hasVisibleKeyboardFocus (*component, true));
                const auto keyboard = component->createComponentSnapshot (component->getLocalBounds()).getPixelAt (point.x, point.y);
                expect (normal != keyboard, "Tab focus retains visible skin feedback");
                if (component == &effects) component->setVisible (false);
            }
        }

        beginTest ("Delete ignores hidden selections and a no-match filter");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            const auto original = fixture.entries();
            fixture.select ({ 0, 1 });
            fixture.panel.setQuery ("no such song");
            expectEquals (fixture.table().getNumSelectedRows(), 0);
            expect (fixture.table().keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
            expect (fixture.entries() == original);
            fixture.panel.setQuery ("Zulu");
            expectEquals (fixture.table().getNumSelectedRows(), 1);
            fixture.table().keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
            expect (fixture.entries() == std::vector<PlaylistEntry> { original[1], original[2], original[3] });
            fixture.panel.setQuery ({});
            expectEquals (fixture.table().getNumSelectedRows(), 1);
            expectEquals (fixture.table().getSelectedRow(), 0);
        }

        beginTest ("queue quick actions retain the clicked occurrence without changing the browser");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            expect (fixture.controller.enqueueLibraryTracks ({ fixture.ids[0], fixture.ids[0], fixture.ids[1] }, false));
            const auto queue = fixture.controller.getPlaylist().getTracks();
            fixture.panel.showQueueTrackActions (queue[1], fixture.table());
            auto menu = juce::Component::getCurrentlyModalComponent();
            expect (fixture.chooseAction (menu, 4));
            expect (fixture.controller.getLibrary().isFavourite (fixture.ids[0]));
            fixture.panel.showQueueTrackActions (queue[1], fixture.table());
            menu = juce::Component::getCurrentlyModalComponent();
            expect (fixture.chooseAction (menu, 6));
            expectEquals (fixture.controller.getPlaylist().size(), 2);
            expectEquals (fixture.controller.getPlaylist().getTrack (0)->id, queue[0].id);
            expectEquals (fixture.controller.getLibraryCollectionId(), "playlist:" + fixture.playlistId);
        }

        beginTest ("new playlist quick action includes the selected songs");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            fixture.select ({ 1, 3 });
            auto menu = fixture.openActions();
            expect (fixture.chooseAction (menu, 9));
            const auto* playlist = fixture.controller.getLibrary().getPlaylist (
                fixture.controller.getLibraryCollectionId().substring (9));
            expect (playlist != nullptr);
            if (playlist != nullptr)
            {
                expectEquals (static_cast<int> (playlist->entries.size()), 2);
                expectEquals (playlist->entries[0].trackId, fixture.ids[1]);
                expectEquals (playlist->entries[1].trackId, fixture.ids[2]);
            }
        }

        beginTest ("favourites belongs to Library and queue requests preserve the browsing page");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            bool queueRequested = false;
            fixture.panel.onShowQueue = [&] { queueRequested = true; };
            fixture.panel.selectCollection ("favourites");
            fixture.panel.updateNavigation (true);
            expect (dynamic_cast<juce::Button*> (fixture.bindings.find ("library.open"))->getToggleState());
            const auto before = fixture.panel.currentTracks();
            fixture.panel.selectCollection ("queue");
            expect (queueRequested);
            expect (fixture.panel.page() == MusicLibraryPanel::Page::collection);
            expectEquals (static_cast<int> (fixture.panel.currentTracks().size()), static_cast<int> (before.size()));
            expectEquals (fixture.controller.getLibraryCollectionId(), juce::String ("favourites"));
        }

        beginTest ("context removal uses visible selection and its captured collection");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            const auto original = fixture.entries();
            fixture.select ({ 0, 1 });
            fixture.panel.setQuery ("Zulu");
            auto menu = fixture.openActions();
            expect (menu != nullptr);
            fixture.panel.selectCollection ("all");
            expect (fixture.chooseAction (menu, 6), "Remove menu callback completed with the requested action");
            expect (fixture.entries() == std::vector<PlaylistEntry> { original[1], original[2], original[3] });
            expectEquals (fixture.controller.getLibraryCollectionId(), juce::String ("all"));
        }

        beginTest ("sorted multiselect preserves queue order and the exact playing occurrence");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            fixture.select ({ 0, 1, 2 });
            fixture.model().sortOrderChanged (1, true);
            expect (fixture.table().isRowSelected (0) && fixture.table().isRowSelected (2)
                    && fixture.table().isRowSelected (3) && ! fixture.table().isRowSelected (1));
            const auto firstBefore = fixture.markerPixel (0);
            const auto lastBefore = fixture.markerPixel (3);
            auto menu = fixture.openActions();
            expect (menu != nullptr);
            expect (fixture.chooseAction (menu, 1), "Play menu callback completed with the requested action");
            const auto& queue = fixture.controller.getPlaylist().getTracks();
            expectEquals (static_cast<int> (queue.size()), 3);
            if (queue.size() == 3)
            {
                expectEquals (queue[0].libraryId, fixture.ids[1]);
                expectEquals (queue[1].libraryId, fixture.ids[0]);
                expectEquals (queue[2].libraryId, fixture.ids[0]);
                expect (queue[1].id != queue[2].id);
                expectEquals (fixture.controller.getLoadedTrackId(), queue[0].id);
            }
            expect (fixture.markerPixel (0) != firstBefore);
            expect (fixture.markerPixel (3) == lastBefore);
        }

        beginTest ("selection and search refreshes do not load a track or follow playback");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            expect (fixture.controller.playLibraryTracks ({ fixture.ids[0], fixture.ids[1] }));
            fixture.panel.refresh();
            const auto loaded = fixture.controller.getLoadedTrackId();
            fixture.select ({ 1, 3 });
            fixture.panel.setQuery ("Alpha");
            expectEquals (fixture.controller.getLoadedTrackId(), loaded);
            expect (fixture.controller.nextTrack());
            fixture.panel.refresh();
            expectEquals (fixture.table().getNumSelectedRows(), 1);
            expectEquals (fixture.table().getSelectedRow(), 0);
            fixture.panel.setQuery ({});
            expect (fixture.table().isRowSelected (1) && fixture.table().isRowSelected (3));
            expectEquals (fixture.table().getNumSelectedRows(), 2);
            fixture.panel.selectCollection ("queue");
            fixture.select ({ 0 });
            const auto nowLoaded = fixture.controller.getLoadedTrackId();
            expect (fixture.controller.getViewState().isPlaying);
            fixture.panel.refresh();
            expectEquals (fixture.controller.getLoadedTrackId(), nowLoaded);
            expectEquals (fixture.table().getSelectedRow(), 0);
        }

        beginTest ("internal drag keeps duplicate occurrence order and is disabled while filtered");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            const auto original = fixture.entries();
            fixture.select ({ 0, 2 });
            const auto description = fixture.model().getDragSourceDescription (fixture.table().getSelectedRows());
            const auto values = description.getProperty ("entries", {});
            expect (values.isArray() && values.size() == 2);
            if (values.isArray() && values.size() == 2)
            {
                expectEquals (values[0].toString(), original[0].id);
                expectEquals (values[1].toString(), original[2].id);
            }
            auto& target = dynamic_cast<juce::DragAndDropTarget&> (fixture.table());
            const juce::DragAndDropTarget::SourceDetails details (description, &fixture.table(), { 20, 350 });
            expect (target.isInterestedInDragSource (details));
            target.itemDropped (details);
            expect (fixture.entries() == std::vector<PlaylistEntry> { original[1], original[3], original[0], original[2] });
            fixture.panel.setQuery ("Zulu");
            expect (fixture.model().getDragSourceDescription (fixture.table().getSelectedRows()).isVoid());
            expect (! target.isInterestedInDragSource (details));
        }

        beginTest ("mixed play-next and append preserve each source playlist occurrence marker");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            const auto originalCollection = "playlist:" + fixture.playlistId;
            const auto originalMarker = fixture.markerPixel (3);
            fixture.select ({ 3 });
            auto menu = fixture.openActions();
            expect (fixture.chooseAction (menu, 1), "Play begins from the first collection");
            expectEquals (fixture.controller.getPlaylist().size(), 1);
            const auto* originalTrack = fixture.controller.getPlaylist().getTrack (0);
            const auto originalQueueId = originalTrack != nullptr ? originalTrack->id : juce::String {};
            const auto second = fixture.controller.createSavedPlaylist ("Mixed queue source");
            expect (fixture.controller.addTracksToSavedPlaylist (second, { fixture.ids[0], fixture.ids[1], fixture.ids[0] }));
            const auto secondCollection = "playlist:" + second;
            fixture.panel.selectCollection (secondCollection);
            fixture.select ({ 2 });
            menu = fixture.openActions();
            expect (fixture.chooseAction (menu, 2), "Play next captures the second repeated occurrence");
            fixture.select ({ 0 });
            menu = fixture.openActions();
            expect (fixture.chooseAction (menu, 3), "Append captures the first repeated occurrence");
            fixture.select ({});
            const auto firstIdle = fixture.markerPixel (0);
            const auto secondIdle = fixture.markerPixel (2);
            expectEquals (fixture.controller.getPlaylist().size(), 3);
            if (fixture.controller.getPlaylist().size() == 3)
            {
                expectEquals (fixture.controller.getPlaylist().getTrack (0)->id, originalQueueId);
                expect (fixture.controller.getPlaylist().getTrack (1)->id != fixture.controller.getPlaylist().getTrack (2)->id);
            }
            expect (fixture.controller.nextTrack());
            fixture.panel.refresh();
            expect (fixture.markerPixel (0) == firstIdle);
            expect (fixture.markerPixel (2) != secondIdle, "The queued source row is marked, not every reference to the recording");
            expect (fixture.controller.nextTrack());
            fixture.panel.refresh();
            expect (fixture.markerPixel (0) != firstIdle);
            expect (fixture.markerPixel (2) == secondIdle);
            expect (fixture.controller.previousTrack());
            expect (fixture.controller.previousTrack());
            fixture.panel.selectCollection (originalCollection);
            fixture.select ({});
            expectEquals (fixture.controller.getLoadedTrackId(), originalQueueId);
            expect (fixture.markerPixel (3) != originalMarker, "Appending another collection retains the first collection's source mapping");
        }

        beginTest ("overview title survives a library refresh after leaving a collection");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            fixture.panel.showLibrary();
            fixture.panel.refresh();
            auto* title = dynamic_cast<juce::Label*> (fixture.bindings.find ("library.title"));
            expect (title != nullptr);
            if (title != nullptr) expectEquals (title->getText(), juce::String ("Library"));
            expectEquals (fixture.controller.getLibraryCollectionId(), "playlist:" + fixture.playlistId);
        }

        beginTest ("overview refresh invalidates visible and hidden metadata projections across categories");
        {
            auto tagged = prepared;
            for (auto& item : tagged)
            {
                item.track.album = "First album";
                item.track.artist = "First artist";
                item.track.artists = { "First artist" };
            }
            LibraryPanelFixture fixture (skin, root, tagged);
            auto* grid = dynamic_cast<PlaylistGridComponent*> (fixture.bindings.find ("library.grid"));
            auto* search = dynamic_cast<juce::TextEditor*> (fixture.bindings.find ("library.gridSearch"));
            expect (grid != nullptr && search != nullptr);
            if (grid != nullptr && search != nullptr)
            {
                grid->setBounds (0, 80, 640, 400);
                const auto browse = [&] (const juce::String& id)
                {
                    dynamic_cast<juce::Button*> (fixture.bindings.find (id))->onClick();
                    fixture.panel.updateNavigation (true);
                };
                browse ("library.browseAlbums");
                expectEquals (grid->visibleCollectionCount(), 1);
                auto changed = tagged[0];
                changed.track.album = "Second album";
                changed.track.artist = "Second artist";
                changed.track.artists = { "Second artist" };
                changed.track.artworkCacheKey = "changed-artwork";
                MusicDirectoryScanResult scan;
                scan.updates.push_back (changed);
                expect (fixture.controller.applyMusicDirectoryScan (std::move (scan)));
                fixture.panel.refresh();
                expectEquals (grid->visibleCollectionCount(), 2);
                search->setText ("Second album");
                expect (LibraryPanelFixture::waitUntil ([&] { return grid->visibleCollectionCount() == 1; }));
                for (int width : { 620, 600, 640 })
                {
                    grid->setSize (width, 400);
                    fixture.panel.updateNavigation (true);
                    expectEquals (grid->visibleCollectionCount(), 1);
                }
                browse ("library.browseArtists");
                expectEquals (grid->visibleCollectionCount(), 2);
                grid->setVisible (false);
                scan = {};
                for (const auto& record : fixture.controller.getLibrary().getTracks())
                {
                    TrackMetadataResult cleared;
                    cleared.ok = true;
                    cleared.track = record;
                    cleared.track.album.clear();
                    cleared.track.artist.clear();
                    cleared.track.artists.clear();
                    scan.updates.push_back (std::move (cleared));
                }
                expect (fixture.controller.applyMusicDirectoryScan (std::move (scan)));
                fixture.panel.refresh();
                grid->setVisible (true);
                fixture.panel.updateNavigation (true);
                expectEquals (grid->visibleCollectionCount(), 0);
                browse ("library.browseAlbums");
                expectEquals (grid->visibleCollectionCount(), 0);
                browse ("library.browsePlaylists");
                expectEquals (grid->visibleCollectionCount(), 2);
                expect (fixture.controller.renameSavedPlaylist (fixture.playlistId, "Renamed playlist"));
                fixture.panel.refresh();
                search->setText ("Renamed playlist");
                expect (LibraryPanelFixture::waitUntil ([&] { return grid->visibleCollectionCount() == 1; }));
            }
        }

        beginTest ("detail summary refresh follows favourites availability and playlist edits");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            auto* description = dynamic_cast<juce::Label*> (fixture.bindings.find ("library.description"));
            expect (description != nullptr);
            if (description != nullptr)
            {
                fixture.panel.selectCollection ("favourites");
                expect (description->getText().startsWith ("0 songs"));
                expect (fixture.controller.setTrackFavourites ({ fixture.ids[0] }, true));
                fixture.panel.refresh();
                fixture.panel.updateNavigation (true);
                expect (description->getText().startsWith ("1 song"));
                SmartPlaylistRules rules;
                rules.rules.push_back ({ SmartPlaylistField::available, SmartPlaylistOperator::equals, {}, 0, true });
                const auto smart = fixture.controller.createSmartPlaylist ("Available songs", rules);
                fixture.panel.selectCollection ("playlist:" + smart);
                expect (description->getText().contains ("3 songs"));
                MusicDirectoryScanResult scan;
                scan.missingTrackIds.add (fixture.ids[0]);
                expect (fixture.controller.applyMusicDirectoryScan (std::move (scan)));
                fixture.panel.refresh();
                expect (description->getText().contains ("2 songs"));
                fixture.panel.selectCollection ("playlist:" + fixture.playlistId);
                expect (description->getText().startsWith ("4 songs"));
                expect (fixture.controller.addTracksToSavedPlaylist (fixture.playlistId, { fixture.ids[1] }));
                fixture.panel.refresh();
                fixture.panel.updateNavigation (true);
                expect (description->getText().startsWith ("5 songs"));
            }
        }

        beginTest ("collection play controls follow the visible playable search results");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            auto* play = fixture.bindings.find ("library.play");
            auto* shuffle = fixture.bindings.find ("library.shufflePlay");
            expect (play != nullptr && shuffle != nullptr);
            if (play != nullptr && shuffle != nullptr)
            {
                expect (play->isEnabled() && shuffle->isEnabled());
                fixture.panel.setQuery ("no matching recording");
                expect (! play->isEnabled() && ! shuffle->isEnabled());
                fixture.panel.setQuery ({});
                expect (play->isEnabled() && shuffle->isEnabled());
            }
        }

        beginTest ("export keeps the menu-opening collection snapshot");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            std::vector<TrackItem> exported;
            fixture.panel.onExportPlaylist = [&] (const auto& rows) { exported = rows; };
            const auto expected = fixture.panel.currentTracks();
            auto menu = fixture.openActions();
            expect (menu != nullptr);
            fixture.panel.selectCollection ("all");
            expect (fixture.chooseAction (menu, 41), "Export menu callback completed with the requested action");
            expectEquals (static_cast<int> (exported.size()), static_cast<int> (expected.size()));
            if (exported.size() == expected.size())
                for (size_t index = 0; index < expected.size(); ++index)
                    expectEquals (exported[index].id, expected[index].id);
        }

        beginTest ("smart playlist menus edit captured rules and keep result rows read-only");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            SmartPlaylistRules rules { SmartPlaylistMatch::all, {
                { SmartPlaylistField::available, SmartPlaylistOperator::equals, {}, 0, true } } };
            const auto smart = fixture.controller.createSmartPlaylist ("Available songs", rules);
            expect (smart.isNotEmpty());
            fixture.panel.selectCollection ("playlist:" + smart);
            fixture.select ({ 0 });
            expect (fixture.model().getDragSourceDescription (fixture.table().getSelectedRows()).isVoid());
            const auto count = fixture.panel.currentTracks().size();
            fixture.table().keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
            expectEquals (static_cast<int> (fixture.panel.currentTracks().size()), static_cast<int> (count));
            fixture.model().sortOrderChanged (1, false);
            expect (fixture.controller.getLibrary().getPlaylist (smart)->entries.empty());
            juce::String edited;
            fixture.panel.onEditSmartPlaylist = [&] (const auto& id) { edited = id; };
            auto menu = fixture.openDetailMenu ("library.title");
            expect (menu != nullptr);
            if (menu != nullptr)
            {
                const auto titles = LibraryPanelFixture::menuTitles (*menu);
                expect (titles.contains ("Edit rules..."));
                expect (titles.contains ("Save as regular playlist"));
            }
            fixture.panel.selectCollection ("playlist:" + fixture.playlistId);
            expect (fixture.chooseAction (menu, 43));
            expectEquals (edited, smart);
            fixture.panel.showLibrary();
            menu = fixture.openActions();
            expect (menu != nullptr);
            if (menu != nullptr) expect (LibraryPanelFixture::menuTitles (*menu).contains ("New smart playlist..."));
            expect (fixture.chooseAction (menu, 42));
            expect (edited.isEmpty());
        }

        beginTest ("queue row menu exposes upcoming moves and whole-queue actions");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            expect (fixture.controller.enqueueLibraryTracks (fixture.ids, false));
            fixture.panel.showQueueTrackActions (*fixture.controller.getPlaylist().getTrack (1), fixture.table());
            fixture.activeMenu = juce::Component::getCurrentlyModalComponent();
            expect (fixture.activeMenu != nullptr);
            if (fixture.activeMenu != nullptr)
            {
                const auto titles = LibraryPanelFixture::menuTitles (*fixture.activeMenu);
                for (const auto* item : { "Move up", "Move down", "Save queue as playlist", "Export queue...", "Clear queue" })
                    expect (titles.contains (item), item);
            }
            expect (fixture.chooseAction (fixture.activeMenu, 10));
            expect (fixture.controller.getPlaylist().isEmpty());
            fixture.panel.showQueueActions (fixture.table());
            fixture.activeMenu = juce::Component::getCurrentlyModalComponent();
            expect (fixture.activeMenu != nullptr);
            if (fixture.activeMenu != nullptr)
                expect (LibraryPanelFixture::menuTitles (*fixture.activeMenu).contains ("Undo list edit"));
            expect (fixture.chooseAction (fixture.activeMenu, 7));
            expectEquals (fixture.controller.getPlaylist().size(), static_cast<int> (fixture.ids.size()));
        }

        beginTest ("detail cover and title share playlist actions and retain the menu-opening target");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            const auto other = fixture.controller.createSavedPlaylist ("Second playlist");
            juce::String coverTarget;
            fixture.panel.onChoosePlaylistArtwork = [&] (const auto& id) { coverTarget = id; };
            auto menu = fixture.openDetailMenu ("library.detailCover");
            expect (menu != nullptr);
            if (menu != nullptr)
            {
                const auto titles = LibraryPanelFixture::menuTitles (*menu);
                expect (! titles.contains ("Open"));
                for (const auto* text : { "Rename", "Duplicate", "Change cover...", "Use automatic cover", "Delete playlist" })
                    expect (titles.contains (text), text);
            }
            fixture.panel.selectCollection ("playlist:" + other);
            expect (fixture.chooseAction (menu, 27));
            expectEquals (coverTarget, fixture.playlistId);
            fixture.panel.selectCollection ("playlist:" + fixture.playlistId);
            menu = fixture.openDetailMenu ("library.title");
            expect (fixture.chooseAction (menu, 21));
            auto* editor = fixture.renameEditor();
            expect (editor != nullptr);
            if (editor != nullptr)
            {
                editor->setText ("Renamed from context", false);
                fixture.panel.finishEditing();
                expectEquals (fixture.controller.getLibrary().getPlaylist (fixture.playlistId)->name,
                              juce::String ("Renamed from context"));
            }
            menu = fixture.openDetailMenu ("library.detailCover");
            fixture.panel.selectCollection ("playlist:" + other);
            expect (fixture.chooseAction (menu, 23));
            expect (fixture.controller.getLibrary().getPlaylist (fixture.playlistId) == nullptr);
            expect (fixture.controller.getLibrary().getPlaylist (other) != nullptr);
            expectEquals (static_cast<int> (fixture.controller.getLibrary().getTracks().size()), 3);
            for (const auto& track : fixture.controller.getLibrary().getTracks()) expect (track.file.existsAsFile());
            fixture.panel.selectCollection ("favourites");
            menu = fixture.openDetailMenu ("library.title");
            expect (menu != nullptr);
            if (menu != nullptr)
            {
                const auto titles = LibraryPanelFixture::menuTitles (*menu);
                expect (! titles.contains ("Open"));
                expect (! titles.contains ("Rename") && ! titles.contains ("Delete playlist") && ! titles.contains ("Change cover..."));
            }
            expect (fixture.chooseAction (menu, 0));
        }

        beginTest ("rename keeps the menu-opening playlist target");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            auto menu = fixture.openActions();
            expect (menu != nullptr);
            const auto other = fixture.controller.createSavedPlaylist ("Second playlist");
            fixture.panel.selectCollection ("playlist:" + other);
            expect (fixture.chooseAction (menu, 21), "Rename menu callback completed with the requested action");
            expectEquals (fixture.controller.getLibraryCollectionId(), "playlist:" + fixture.playlistId);
            juce::TextEditor* editor = nullptr;
            for (auto* child : fixture.panel.collectionsControl().getChildren())
                if (auto* label = dynamic_cast<juce::Label*> (child)) editor = label->getCurrentTextEditor();
            expect (editor != nullptr);
            if (editor != nullptr)
            {
                editor->setText ("Renamed first", false);
                fixture.panel.finishEditing();
                expectEquals (fixture.controller.getLibrary().getPlaylist (fixture.playlistId)->name,
                              juce::String ("Renamed first"));
                expectEquals (fixture.controller.getLibrary().getPlaylist (other)->name,
                              juce::String ("Second playlist"));
            }
        }
        beginTest ("a dispatched New playlist click keeps the new editor alive after the old editor closes");
        {
            LibraryPanelFixture fixture (skin, root, prepared);
            fixture.host.toFront (true);
            expect (fixture.clickNewPlaylist(), "The JUCE peer must deliver the New playlist button hit");
            const auto first = fixture.controller.getLibraryCollectionId().substring (9);
            expect (first != fixture.playlistId);
            expect (fixture.renameEditor() != nullptr);
            if (auto* editor = fixture.renameEditor())
            {
                editor->setText ("First retained name", false);
                expect (fixture.clickNewPlaylist(), "The next click must reach the same native button");
                const auto second = fixture.controller.getLibraryCollectionId().substring (9);
                expect (second != first);
                expectEquals (fixture.controller.getLibrary().getPlaylist (first)->name,
                              juce::String ("First retained name"));
                juce::Component::SafePointer<juce::TextEditor> nextEditor (fixture.renameEditor());
                expect (nextEditor != nullptr);
                auto dispatched = std::make_shared<bool> (false);
                juce::MessageManager::callAsync ([dispatched] { *dispatched = true; });
                expect (LibraryPanelFixture::waitUntil ([&] { return *dispatched; }));
                expect (nextEditor != nullptr, "An older editor completion must not close the replacement editor");
                if (nextEditor != nullptr)
                {
                    expect (nextEditor->hasKeyboardFocus (false));
                    nextEditor->setText ("Second", false);
                    nextEditor->setCaretPosition (nextEditor->getText().length());
                    expect (fixture.host.getPeer()->handleKeyPress (juce::KeyPress::spaceKey, ' '));
                    expectEquals (nextEditor->getText(), juce::String ("Second "));
                    nextEditor->insertTextAtCaret ("playlist");
                    fixture.panel.finishEditing();
                    expectEquals (fixture.controller.getLibrary().getPlaylist (second)->name,
                                  juce::String ("Second playlist"));
                }
            }
        }
        root.deleteRecursively();
    }
};

MusicLibraryPanelTests musicLibraryPanelTests;
}
