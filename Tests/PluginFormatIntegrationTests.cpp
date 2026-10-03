#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "plugins/PluginManager.h"
#include "plugins/PluginScanProcess.h"
#include "app/PlayerController.h"
#include "app/AutoSaveScheduler.h"

#include <cmath>

namespace
{
class ScopeOfflineBackend final : public IAudioDeviceBackend
{
public:
    juce::String initialiseWithDefaultDevices (int, int) override { return "Offline scope fixture."; }
    juce::StringArray getOutputDeviceNames() const override { return {}; }
    AudioDeviceBackendState getCurrentDeviceState() const override { return {}; }
    juce::AudioDeviceManager::AudioDeviceSetup getAudioDeviceSetup() const override { return {}; }
    juce::String setAudioDeviceSetup (const juce::AudioDeviceManager::AudioDeviceSetup&, bool) override { return "Offline."; }
    void addAudioCallback (juce::AudioIODeviceCallback*) override {}
    void removeAudioCallback (juce::AudioIODeviceCallback*) override {}
};

bool dispatchScopeUntil (const std::function<bool()>& complete)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    while (! complete() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return complete();
}

struct ScopedTemporaryDirectory
{
    ~ScopedTemporaryDirectory()
    {
        static_cast<void> (directory.deleteRecursively());
    }

    juce::File directory;
};

juce::Array<juce::PluginDescription> scanWithHelper (const juce::String& format,
                                                     const juce::String& candidate,
                                                     juce::String& error)
{
    ScopedTemporaryDirectory temporary {
        juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getNonexistentChildFile ("moonwake-format-integration", juce::String())
    };
    if (! temporary.directory.createDirectory())
    {
        error = "Could not create an isolated plug-in scan result directory.";
        return {};
    }

    const auto output = temporary.directory.getChildFile ("plugins.xml");
    const auto result = PluginScanProcess::run (
        { MOONWAKE_PLUGIN_SCANNER_PATH, "--scan", format, candidate,
          output.getFullPathName() },
        30000,
        [] { return false; });

    if (result.outcome != PluginScanProcessOutcome::completed)
    {
        error = result.diagnostic;
        return {};
    }

    auto xml = juce::XmlDocument::parse (output);
    if (xml == nullptr)
    {
        error = "The isolated scanner did not produce a valid plug-in list.";
        return {};
    }

    juce::KnownPluginList known;
    known.recreateFromXml (*xml);
    return known.getTypes();
}

bool prepareTenChannelBuffer (juce::AudioPluginInstance& instance,
                              juce::AudioBuffer<float>& buffer,
                              juce::String& error)
{
    juce::AudioProcessor::BusesLayout layout;
    const auto channels = juce::AudioChannelSet::create7point1point2();
    layout.inputBuses.add (channels);
    layout.outputBuses.add (channels);
    if (! instance.setBusesLayout (layout))
    {
        error = "The plug-in rejected a matching 10-in/10-out layout.";
        return false;
    }

    if (instance.getTotalNumInputChannels() != 10
        || instance.getTotalNumOutputChannels() != 10
        || instance.getBusesLayout().getMainInputChannelSet() != channels
        || instance.getBusesLayout().getMainOutputChannelSet() != channels)
    {
        error = "The plug-in did not advertise the active 10-in/10-out 7.1.2 layout.";
        return false;
    }

    instance.setNonRealtime (true);
    instance.prepareToPlay (48000.0, 64);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample (channel, sample,
                              static_cast<float> (channel + 1)
                                  + static_cast<float> (sample) * 0.001f);

    return true;
}

bool processFixtureAcrossTenChannels (juce::AudioPluginInstance& instance,
                                      juce::String& error)
{
    juce::AudioBuffer<float> buffer (10, 64);
    if (! prepareTenChannelBuffer (instance, buffer, error))
        return false;

    juce::MidiBuffer midi;
    instance.processBlock (buffer, midi);
    instance.releaseResources();

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto input = static_cast<float> (channel + 1)
                             + static_cast<float> (sample) * 0.001f;
            if (! juce::approximatelyEqual (buffer.getSample (channel, sample),
                                            input * 0.5f))
            {
                error = "The VST3 fixture did not preserve and process all 10 channels.";
                return false;
            }
        }

    return true;
}

bool processAudioUnitAcrossTenChannels (juce::AudioPluginInstance& instance,
                                        juce::String& error)
{
    juce::AudioBuffer<float> buffer (10, 64);
    if (! prepareTenChannelBuffer (instance, buffer, error))
        return false;

    juce::MidiBuffer midi;
    instance.processBlock (buffer, midi);
    instance.releaseResources();

    float previousMean = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        double total = 0.0;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto value = buffer.getSample (channel, sample);
            if (! std::isfinite (value))
            {
                error = "AUNBandEQ produced a non-finite sample.";
                return false;
            }
            total += std::abs (value);
        }

        const auto mean = static_cast<float> (total / buffer.getNumSamples());
        if (mean <= 0.001f)
        {
            error = "AUNBandEQ dropped an active output channel.";
            return false;
        }
        if (channel > 0 && mean <= previousMean + 0.01f)
        {
            error = "AUNBandEQ collapsed distinct channels while processing 7.1.2 audio.";
            return false;
        }
        previousMean = mean;
    }

    return true;
}

bool clearNativeTails (std::unique_ptr<juce::AudioPluginInstance> instance, juce::String& error)
{
    auto* processor = instance.get();
    EffectGraph graph;
    graph.prepareToPlay (48000.0, 64, juce::AudioChannelSet::create7point1point2());
    if (! graph.addProcessor (1, std::move (instance), error)) return false;
    std::vector<float> parameters;
    for (auto* parameter : processor->getParameters()) parameters.push_back (parameter->getValue());
    if (! graph.clearTails (error)) return false;
    for (int i = 0; i < processor->getParameters().size(); ++i)
        if (std::abs (processor->getParameters()[i]->getValue() - parameters[static_cast<size_t> (i)]) > 0.00001f)
        {
            error = "Clearing native effect tails changed a parameter.";
            return false;
        }
    juce::AudioBuffer<float> buffer (10, 64);
    for (int channel = 0; channel < 10; ++channel)
        std::fill_n (buffer.getWritePointer (channel), 64, 0.1f);
    juce::MidiBuffer midi;
    graph.processBlock (buffer, midi);
    for (int channel = 0; channel < 10; ++channel)
        if (! std::isfinite (buffer.getMagnitude (channel, 0, 64)) || buffer.getMagnitude (channel, 0, 64) <= 0.0f)
        {
            error = "The native effect did not resume processing after clearing tails.";
            return false;
        }
    return true;
}

class PluginFormatIntegrationTests final : public juce::UnitTest
{
public:
    PluginFormatIntegrationTests()
        : UnitTest ("PluginFormatIntegration", "plugins")
    {
    }

    void runTest() override
    {
        beginTest ("controller inserts, changes, rejects and restores a real VST3 channel scope");
        {
            juce::String error;
            const auto descriptions = scanWithHelper ("VST3", MOONWAKE_TEST_VST3_PATH, error);
            expectEquals (descriptions.size(), 1, error);
            if (! descriptions.isEmpty())
            {
                {
                    PlayerController catalogController (std::make_unique<ScopeOfflineBackend>());
                    juce::KnownPluginList displayedCatalog;
                    displayedCatalog.addType (descriptions[0]);
                    StateRestoreResult displayed;
                    displayed.state.emplace();
                    displayed.pluginCache = displayedCatalog.createXml();
                    expect (catalogController.applyPreparedState (std::move (displayed)));
                    const auto selectedIdentifier = catalogController.getInsertEffectPlugins()[0].createIdentifierString();

                    auto newlyDiscovered = descriptions[0];
                    newlyDiscovered.name = "AAA Newly Discovered Effect";
                    newlyDiscovered.fileOrIdentifier = "/missing/newly-discovered.vst3";
                    newlyDiscovered.uniqueId = 91234;
                    juce::KnownPluginList refreshedCatalog;
                    refreshedCatalog.addType (newlyDiscovered);
                    refreshedCatalog.addType (descriptions[0]);
                    StateRestoreResult refreshed;
                    refreshed.state.emplace();
                    refreshed.pluginCache = refreshedCatalog.createXml();
                    expect (catalogController.applyPreparedState (std::move (refreshed)));
                    expect (catalogController.getInsertEffectPlugins()[0].createIdentifierString() != selectedIdentifier);
                    bool done = false, insertedSelection = false;
                    catalogController.addPluginByIdentifierAsync (selectedIdentifier, [&] (bool inserted, const juce::String& message)
                    {
                        done = true;
                        insertedSelection = inserted;
                        error = message;
                    });
                    expect (dispatchScopeUntil ([&] { return done; }));
                    expect (insertedSelection, "A displayed selection must survive catalog reordering: " + error);
                    if (insertedSelection)
                        expectEquals (catalogController.getEffectChain().getSlot (0)->pluginIdentifier, selectedIdentifier);
                }
                juce::KnownPluginList known;
                known.addType (descriptions[0]);
                {
                    PlayerController pending (std::make_unique<ScopeOfflineBackend>());
                    StateRestoreResult initialPending;
                    initialPending.state.emplace();
                    initialPending.pluginCache = known.createXml();
                    expect (pending.applyPreparedState (std::move (initialPending)));
                    bool finished = false, accepted = false;
                    pending.addPluginByIdentifierAsync (descriptions[0].createIdentifierString(), [&] (bool ok, const juce::String& message)
                    {
                        finished = true;
                        accepted = ok;
                        error = message;
                    });
                    expect (! finished);
                    expect (pending.setPluginFormatPreferences ({ false, false }));
                    expect (dispatchScopeUntil ([&] { return finished; }));
                    expect (! accepted && error.contains ("disabled"), error);
                    expectEquals (pending.getEffectChain().size(), 0);
                }
                PlayerController controller (std::make_unique<ScopeOfflineBackend>());
                StateRestoreResult initial;
                initial.state.emplace();
                initial.state->outputLayout = OutputLayout::sevenOneTwo;
                initial.pluginCache = known.createXml();
                expect (controller.applyPreparedState (std::move (initial)));
                expectEquals (controller.getInsertEffectPlugins().size(), 1);
                const EffectScope sides { juce::AudioChannelSet::leftSurroundSide, juce::AudioChannelSet::rightSurroundSide };
                bool completed = false;
                bool inserted = false;
                controller.addPluginByIdentifierAsync (descriptions[0].createIdentifierString(), [&] (bool ok, const juce::String& message)
                {
                    completed = true;
                    inserted = ok;
                    error = message;
                }, sides);
                expect (dispatchScopeUntil ([&] { return completed; }));
                expect (inserted, error);
                if (inserted)
                {
                    expect (controller.getEffectChain().getSlot (0)->scope == sides);
                    auto* instance = controller.getEffectPluginForEditor (0);
                    expect (instance != nullptr && instance->getTotalNumInputChannels() == 2);
                    expect (controller.setEffectBypassedAtIndex (0, true));
                    const auto revision = controller.getEffectChainRevision();
                    expect (! controller.setEffectScopeAtIndex (0, { juce::AudioChannelSet::left, juce::AudioChannelSet::left }));
                    expectEquals (controller.getEffectChainRevision(), revision);
                    expect (controller.getEffectChain().getSlot (0)->scope == sides);
                    expect (controller.setOutputLayout (OutputLayout::stereo).isNotEmpty());
                    expect (controller.getViewState().outputLayout == OutputLayout::sevenOneTwo);
                    const EffectScope mono { juce::AudioChannelSet::LFE };
                    expect (controller.setEffectScopeAtIndex (0, mono), controller.getLastStatusMessage());
                    expect (controller.getEffectPluginForEditor (0)->getBusesLayout().getMainInputChannelSet() == juce::AudioChannelSet::mono());
                    expect (controller.getEffectChain().getSlot (0)->bypassed);
                    PlayerAppState snapshot;
                    expect (controller.setPluginFormatPreferences ({ false, true }));
                    expect (controller.getInsertEffectPlugins().isEmpty());
                    expect (controller.getEffectPluginForEditor (0) == instance,
                            "Disabling new VST3 effects must retain the existing instance.");
                    expect (controller.captureStateSnapshot (snapshot, error), error);
                    expect (! snapshot.pluginFormats.vst3);
                    expect (snapshot.effectChain.getSlot (0)->scope == mono);
                    ScopedTemporaryDirectory temporary { juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getNonexistentChildFile ("moonwake-scope-roundtrip", {}) };
                    expect (temporary.directory.createDirectory());
                    const auto stateFile = temporary.directory.getChildFile ("state.xml");
                    const auto cacheFile = temporary.directory.getChildFile ("plugins.xml");
                    expect (controller.saveState (stateFile, cacheFile), controller.getLastStatusMessage());
                    PlayerController restored (std::make_unique<ScopeOfflineBackend>());
                    expect (restored.loadState (stateFile, cacheFile), restored.getLastStatusMessage());
                    expect (! restored.getPluginFormatPreferences().vst3);
                    expect (dispatchScopeUntil ([&] { return restored.getEffectPluginForEditor (0) != nullptr; }));
                    expect (restored.getEffectChain().getSlot (0)->scope == mono);
                    expect (restored.getEffectChain().getSlot (0)->bypassed);
                    if (auto* restoredInstance = restored.getEffectPluginForEditor (0))
                        expectEquals (restoredInstance->getTotalNumInputChannels(), 1);
                    expect (restored.setEffectScopeAtIndex (0, {}));
                    expectEquals (restored.getEffectPluginForEditor (0)->getTotalNumInputChannels(), 10);
                    expect (restored.setOutputLayout (OutputLayout::stereo).isEmpty());
                    expectEquals (restored.getEffectPluginForEditor (0)->getTotalNumInputChannels(), 2);

                    beginTest ("deleting a pending restored effect prevents its callback from adding an orphan");
                    PlayerController removed (std::make_unique<ScopeOfflineBackend>());
                    expect (removed.loadState (stateFile, cacheFile));
                    expect (removed.getEffectPluginForEditor (0) == nullptr);
                    expect (removed.removeEffectAtIndex (0));
                    expect (dispatchScopeUntil ([&] { return removed.getLastStatusMessage().startsWith ("Saved effect chain restored."); }));
                    expectEquals (removed.getEffectChain().size(), 0);
                    expect (removed.setOutputLayout (OutputLayout::stereo).isEmpty(),
                            "A removed LFE-scoped processor must not remain in the audio graph.");

                    beginTest ("a newer restore invalidates callbacks from the previous session");
                    PlayerController replaced (std::make_unique<ScopeOfflineBackend>());
                    expect (replaced.loadState (stateFile, cacheFile));
                    expect (replaced.getEffectPluginForEditor (0) == nullptr);
                    StateRestoreResult newerSession;
                    newerSession.state = snapshot;
                    newerSession.state->effectChain.getSlotById (snapshot.effectChain.getSlot (0)->id)->scope =
                        { juce::AudioChannelSet::left };
                    expect (replaced.applyPreparedState (std::move (newerSession)));
                    expect (dispatchScopeUntil ([&] { return replaced.getEffectPluginForEditor (0) != nullptr; }));
                    expectEquals (replaced.getEffectChain().size(), 1);
                    expect (replaced.getEffectChain().getSlot (0)->scope == EffectScope { juce::AudioChannelSet::left });
                    expect (replaced.setOutputLayout (OutputLayout::stereo).isEmpty());

                    beginTest ("playing autosaves cache opaque plug-in state until pause and explicit save captures latest state");
                    expect (restored.setEffectBypassedAtIndex (0, false));
                    auto* statefulPlugin = restored.getEffectPluginForEditor (0);
                    expect (statefulPlugin != nullptr);
                    if (statefulPlugin != nullptr)
                    {
                        statefulPlugin->setCurrentProgram (0);
                        statefulPlugin->updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
                        PlayerAppState beforePlayback;
                        expect (restored.captureStateSnapshot (beforePlayback, error), error);
                        const auto cachedState = beforePlayback.effectChain.getSlot (0)->serializedState;
                        const auto audioFile = temporary.directory.getChildFile ("capture.wav");
                        juce::WavAudioFormat wav;
                        auto output = std::unique_ptr<juce::OutputStream> (audioFile.createOutputStream());
                        auto writer = wav.createWriterFor (output, juce::AudioFormatWriterOptions{}
                            .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16));
                        expect (writer != nullptr);
                        if (writer != nullptr)
                        {
                            juce::AudioBuffer<float> silence (2, 48000);
                            silence.clear();
                            expect (writer->writeFromAudioSampleBuffer (silence, 0, silence.getNumSamples()));
                            writer.reset();
                            restored.addFiles ({ audioFile });
                            expect (restored.setPlaying (true));
                            statefulPlugin->setCurrentProgram (1);
                            statefulPlugin->updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
                            const auto playingRevision = restored.getStateRevision();
                            AutoSaveScheduler scheduler;
                            expect (! scheduler.observe (playingRevision, 0.0));
                            expect (scheduler.observe (playingRevision, 1.1));
                            PlayerAppState whilePlaying;
                            expect (restored.captureStateSnapshot (whilePlaying, error), error);
                            expect (restored.isPlaying());
                            expect (whilePlaying.effectChain.getSlot (0)->serializedState == cachedState);
                            scheduler.acknowledge (playingRevision, 1.1, true);
                            expect (! scheduler.observe (playingRevision, 2.2));
                            expect (restored.setPlaying (false));
                            const auto pausedRevision = restored.getStateRevision();
                            expect (pausedRevision > playingRevision);
                            expect (! scheduler.observe (pausedRevision, 3.0));
                            expect (scheduler.observe (pausedRevision, 4.1));
                            PlayerAppState afterPause;
                            expect (restored.captureStateSnapshot (afterPause, error), error);
                            expect (afterPause.effectChain.getSlot (0)->serializedState != cachedState);
                            scheduler.acknowledge (pausedRevision, 4.1, true);
                            expect (! scheduler.observe (pausedRevision, 5.2));

                            expect (restored.setPlaying (true));
                            statefulPlugin->setCurrentProgram (2);
                            statefulPlugin->updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
                            expect (restored.saveState (stateFile, cacheFile), restored.getLastStatusMessage());
                            expect (! restored.isPlaying());
                            PlayerController saved (std::make_unique<ScopeOfflineBackend>());
                            expect (saved.loadState (stateFile, cacheFile));
                            expect (dispatchScopeUntil ([&] { return saved.getEffectPluginForEditor (0) != nullptr; }));
                            if (auto* savedPlugin = saved.getEffectPluginForEditor (0))
                                expectEquals (savedPlugin->getCurrentProgram(), 2);
                        }
                    }
                }
            }
        }

        beginTest ("isolated scanner discovers and hosts a real VST3 bundle");
        {
            juce::String error;
            const auto descriptions = scanWithHelper (
                "VST3", MOONWAKE_TEST_VST3_PATH, error);
            expectEquals (descriptions.size(), 1, error);

            if (! descriptions.isEmpty())
            {
                PluginManager manager;
                auto instance = manager.createPluginInstance (
                    descriptions.getReference (0), 48000.0, 64, error);
                expect (instance != nullptr, error);
                if (instance != nullptr)
                {
                    expect (processFixtureAcrossTenChannels (*instance, error), error);
                    expect (clearNativeTails (std::move (instance), error), error);
                }
            }
        }

       #if JUCE_MAC && JUCE_PLUGINHOST_AU
        beginTest ("isolated scanner hosts Apple's registered AUNBandEQ in 7.1.2");
        {
            juce::AudioUnitPluginFormat audioUnits;
            const auto candidates = audioUnits.searchPathsForPlugins (
                audioUnits.getDefaultLocationsToSearch(), true, true);
            juce::String selected;

            for (const auto& candidate : candidates)
                if (candidate.containsIgnoreCase ("nbeq")
                    && candidate.containsIgnoreCase ("appl"))
                {
                    selected = candidate;
                    break;
                }

            expect (selected.isNotEmpty(),
                    "Apple AUNBandEQ is not registered for host acceptance.");
            if (selected.isNotEmpty())
            {
                juce::String error;
                const auto discovered = scanWithHelper ("AudioUnit", selected, error);
                expectEquals (discovered.size(), 1, error);
                if (! discovered.isEmpty())
                {
                    PluginManager manager;
                    auto instance = manager.createPluginInstance (
                        discovered.getReference (0), 48000.0, 64, error);
                    expect (instance != nullptr, error);
                    if (instance != nullptr)
                    {
                        expect (processAudioUnitAcrossTenChannels (*instance, error), error);
                        expect (clearNativeTails (std::move (instance), error), error);
                    }
                }
            }
        }
       #endif
    }
};

PluginFormatIntegrationTests pluginFormatIntegrationTests;
}
