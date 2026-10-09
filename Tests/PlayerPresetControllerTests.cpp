#include "app/PlayerController.h"
#include "fixtures/LibraryAudioBackend.h"
#include "plugins/PluginScanProcess.h"

#include <limits>
#include <thread>

#if JUCE_MAC || JUCE_LINUX
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace
{
bool dispatchPresetsUntil (const std::function<bool()>& done)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    while (! done() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return done();
}

class PlayerPresetControllerTests final : public juce::UnitTest
{
public:
    PlayerPresetControllerTests() : UnitTest ("PlayerPresetController", "app") {}
    void runTest() override
    {
        beginTest ("create preset controller fixtures");
        using Kind = PlayerController::PresetKind;
        const auto root = juce::File::getCurrentWorkingDirectory().getChildFile ("build")
            .getNonexistentChildFile ("player-presets", {});
        expect (root.createDirectory());
        const auto stateFile = root.getChildFile ("state.xml");
        const auto pluginFile = root.getChildFile ("plugins.xml");
        const auto presetFile = root.getChildFile ("presets.xml");
        juce::String error;

        beginTest ("startup prepares presets independently from session state and reports corrupt libraries");
        {
            ListeningPresetLibrary library;
            EffectChainPreset chain;
            chain.name = "Empty rack";
            expect (library.put (chain).wasOk());
            expect (ListeningPresetStore::writeAtomically (library, presetFile, error), error);
            auto prepared = StateRestoreService::prepare (stateFile, pluginFile, root.getChildFile ("artwork"));
            expect (prepared.presets.has_value());
            expect (prepared.presetFile == presetFile);
            if (prepared.presets) expect (prepared.presets->findEffectChain (chain.id) != nullptr);
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            expect (controller.applyPreparedState (std::move (prepared)));
            expect (controller.getPresetLibrary().findEffectChain (chain.id) != nullptr);
            expect (presetFile.replaceWithText ("broken"));
            prepared = StateRestoreService::prepare (stateFile, pluginFile, root.getChildFile ("artwork"));
            expect (! prepared.presets.has_value());
            expect (! prepared.warnings.isEmpty());
            expect (prepared.errorMessage.isEmpty());
            expect (controller.applyPreparedState (std::move (prepared)));
            expect (controller.getPresetLibrary().findEffectChain (chain.id) != nullptr);
            expect (presetFile.deleteFile());
        }

        beginTest ("CRUD commits memory only after persistence and importing creates an inactive copy");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            expect (controller.loadState (stateFile, pluginFile));
            expect (controller.savePreset (Kind::effectChain, "  Empty chain  ").wasOk());
            const auto id = controller.getActiveEffectPresetId();
            expect (id.isNotEmpty());
            expect (! controller.isEffectPresetModified());
            expect (controller.renamePreset (Kind::effectChain, id, "Renamed").wasOk());
            expect (controller.duplicatePreset (Kind::effectChain, id, "Copy").wasOk());
            expectEquals (controller.getPresetLibrary().getEffectChainPresets().size(), size_t (2));
            const auto exported = root.getChildFile ("chain.moonwake-preset");
            expect (controller.exportPreset (Kind::effectChain, id, exported).wasOk());
            expect (controller.importPreset (exported).wasOk());
            expectEquals (controller.getPresetLibrary().getEffectChainPresets().size(), size_t (3));
            expectEquals (controller.getActiveEffectPresetId(), id);
            expect (presetFile.replaceWithText ("broken"));
            expect (ListeningPresetStore::backupFileFor (presetFile).replaceWithText ("broken backup"));
            expect (controller.renamePreset (Kind::effectChain, id, "Unsaved").failed());
            expectEquals (controller.getPresetLibrary().findEffectChain (id)->name, juce::String ("Renamed"));
            expect (presetFile.deleteFile());
            expect (ListeningPresetStore::backupFileFor (presetFile).deleteFile());
            expect (controller.deletePreset (Kind::effectChain, id).wasOk());
            expect (controller.getActiveEffectPresetId().isEmpty());
        }

        beginTest ("importing beyond the structure budget preserves the in-memory library and both files");
        {
            const auto budgetFile = root.getChildFile ("budget.xml");
            ListeningPresetLibrary library;
            EffectChainPreset preset;
            preset.name = "Scoped rack";
            for (int slot = 0; slot < 128; ++slot)
                preset.chain.addSlot ("EQ", "fixture:eq", "VST3",
                                      { juce::AudioChannelSet::left, juce::AudioChannelSet::right });
            expect (library.put (preset).wasOk());
            expect (ListeningPresetStore::writeAtomically (library, budgetFile, error), error);
            for (int index = 1; index < 194; ++index)
            {
                preset.id = juce::Uuid().toString();
                expect (library.put (preset).wasOk());
            }
            expect (ListeningPresetStore::writeAtomically (library, budgetFile, error), error);
            const auto primary = budgetFile.loadFileAsString();
            const auto backup = ListeningPresetStore::backupFileFor (budgetFile).loadFileAsString();
            const auto importedFile = root.getChildFile ("additional.moonwake-preset");
            expect (ListeningPresetStore::exportPreset (preset, importedFile, error), error);
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            StateRestoreResult prepared;
            prepared.presetFile = budgetFile;
            prepared.presets = std::move (library);
            expect (controller.applyPreparedState (std::move (prepared)));
            expect (controller.importPreset (importedFile).failed());
            expectEquals (controller.getPresetLibrary().getEffectChainPresets().size(), size_t (194));
            expect (budgetFile.loadFileAsString() == primary);
            expect (ListeningPresetStore::backupFileFor (budgetFile).loadFileAsString() == backup);
        }

        beginTest ("listening apply restores outputs without changing volume and dirty tracking excludes test signals");
        {
            auto backend = std::make_unique<LibraryAudioBackend>();
            auto* audio = backend.get();
            PlayerController controller (std::move (backend));
            expect (controller.loadState (stateFile, pluginFile));
            expect (controller.setOutputCalibration ({ juce::AudioChannelSet::left, -3.0f, 5.0, true }).wasOk());
            expect (controller.setOutputRouting ({ 3, 2 }).isEmpty());
            expect (controller.savePreset (Kind::listening, "Room").wasOk());
            const auto id = controller.getActiveListeningPresetId();
            controller.setVolume (0.25);
            controller.setAllEffectsBypassed (true);
            expect (! controller.isListeningPresetModified());
            expect (controller.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine, juce::AudioChannelSet::left, -30.0f, 1000.0 }).wasOk());
            expect (! controller.isListeningPresetModified());
            controller.stopCalibrationTestSignal();
            expect (controller.setOutputCalibration ({ juce::AudioChannelSet::left, -6.0f, 10.0, false }).wasOk());
            expect (controller.isListeningPresetModified());
            bool done = false, applied = false, closed = false;
            expect (controller.applyPreset (Kind::listening, id, [&] (bool ok, const juce::String& message)
            { done = true; applied = ok; error = message; }, [&] { closed = true; }).wasOk());
            expect (controller.isPresetApplyRunning());
            expect (controller.setOutputLayout (OutputLayout::fiveOne).isNotEmpty());
            expect (dispatchPresetsUntil ([&] { return done; }));
            expect (applied, error);
            expect (closed && ! controller.isPresetApplyRunning());
            expect (! controller.isListeningPresetModified());
            expectWithinAbsoluteError (controller.getViewState().volume, 0.25, 0.0001);
            expect (controller.getViewState().allEffectsBypassed);
            const auto calibration = controller.getOutputCalibration();
            const auto left = std::find_if (calibration.begin(), calibration.end(), [] (const auto& value)
                { return value.channel == juce::AudioChannelSet::left; });
            expect (left != calibration.end());
            if (left != calibration.end()) expectWithinAbsoluteError (left->delayMs, 5.0, 0.0001);
            PlayerAppState snapshot;
            expect (controller.captureStateSnapshot (snapshot, error), error);
            expect (snapshot.outputCalibration == calibration);
            expect (controller.saveState (stateFile, pluginFile));
            PlayerController restored (std::make_unique<LibraryAudioBackend>());
            expect (restored.loadState (stateFile, pluginFile));
            expect (restored.getOutputCalibration() == calibration);
            expect (restored.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::off);
            audio->disconnectPhysicalPort (3);
            expect (controller.isListeningPresetModified());
        }

        beginTest ("missing plug-ins fail preflight without closing editors or changing the current chain");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            StateRestoreResult restore;
            restore.presetFile = presetFile;
            restore.presets.emplace();
            EffectChainPreset preset;
            preset.name = "Missing plug-in";
            preset.chain.addSlot ("Missing", "missing:vst3", "VST3");
            expect (restore.presets->put (preset).wasOk());
            expect (controller.applyPreparedState (std::move (restore)));
            bool closed = false, called = false;
            expect (controller.applyPreset (Kind::effectChain, preset.id,
                [&] (bool, const juce::String&) { called = true; }, [&] { closed = true; }).failed());
            expect (! closed && ! called && ! controller.isPresetApplyRunning());
            expect (controller.getEffectChain().isEmpty());
        }

        beginTest ("saving while playing captures real plug-in state and preserves transport during apply");
        {
            const auto cache = root.getChildFile ("fixture-plugins.xml");
            const auto scan = PluginScanProcess::run ({ MOONWAKE_PLUGIN_SCANNER_PATH, "--scan", "VST3",
                MOONWAKE_TEST_VST3_PATH, cache.getFullPathName() }, 30000, [] { return false; });
            expect (scan.outcome == PluginScanProcessOutcome::completed, scan.diagnostic);
            auto xml = juce::XmlDocument::parse (cache);
            expect (xml != nullptr);
            if (xml != nullptr)
            {
                auto backend = std::make_unique<LibraryAudioBackend>();
                auto* audio = backend.get();
                PlayerController controller (std::move (backend));
                StateRestoreResult prepared;
                prepared.pluginCache = std::move (xml);
                prepared.presetFile = root.getChildFile ("real-presets.xml");
                expect (controller.applyPreparedState (std::move (prepared)));
                const auto plugins = controller.getKnownPlugins();
                expect (! plugins.isEmpty());
                if (! plugins.isEmpty())
                {
                    bool inserted = false, complete = false;
                    controller.addPluginByIdentifierAsync (plugins[0].createIdentifierString(), [&](bool ok, const juce::String& message)
                    { inserted = ok; complete = true; error = message; });
                    expect (dispatchPresetsUntil ([&] { return complete; }));
                    expect (inserted, error);
                    auto* plugin = controller.getEffectPluginForEditor (0);
                    if (plugin != nullptr)
                    {
                       #if JUCE_MAC || JUCE_LINUX
                        beginTest ("saving preserves plug-in changes notified from a background thread after capture");
                        for (const auto kind : { Kind::effectChain, Kind::listening })
                        {
                            const auto name = kind == Kind::effectChain ? "Concurrent effect save" : "Concurrent listening save";
                            plugin->setCurrentProgram (1);
                            expect (controller.savePreset (kind, name).wasOk());
                            const auto id = kind == Kind::effectChain ? controller.getActiveEffectPresetId()
                                                                    : controller.getActiveListeningPresetId();
                            const auto target = root.getChildFile ("real-presets.xml");
                            expect (target.copyFileTo (ListeningPresetStore::backupFileFor (target)));
                            expect (target.deleteFile());
                            const auto path = target.getFullPathName().toStdString();
                            const auto created = ::mkfifo (path.c_str(), 0600) == 0;
                            expect (created);
                            if (! created) continue;
                            std::atomic_bool notified { false };
                            std::jthread update ([&] (std::stop_token stop)
                            {
                                // The FIFO opens after persistence has received the captured plug-in state.
                                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
                                int pipe = -1;
                                while (! stop.stop_requested() && juce::Time::getMillisecondCounterHiRes() < deadline)
                                {
                                    pipe = ::open (path.c_str(), O_WRONLY | O_NONBLOCK);
                                    if (pipe >= 0) break;
                                    juce::Thread::sleep (1);
                                }
                                constexpr char damaged[] = "damaged primary";
                                if (pipe < 0)
                                {
                                    if (stop.stop_requested()) return;
                                    // Release both an existing reader and any reader that starts after the timeout.
                                    pipe = ::open (path.c_str(), O_RDWR | O_NONBLOCK);
                                    static_cast<void> (target.replaceWithText (damaged));
                                }
                                else
                                {
                                    plugin->setCurrentProgram (2);
                                    plugin->updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
                                    notified.store (true);
                                }
                                if (pipe >= 0)
                                {
                                    static_cast<void> (::write (pipe, damaged, sizeof (damaged) - 1));
                                    ::close (pipe);
                                }
                            });
                            const auto result = controller.savePreset (kind, name, id);
                            update.request_stop();
                            update.join();
                            expect (result.wasOk(), result.getErrorMessage());
                            expect (notified.load(), "The plug-in notification must occur inside the persistence window.");
                            if (result.failed())
                            {
                                expect (target.deleteFile());
                                expect (ListeningPresetStore::backupFileFor (target).copyFileTo (target));
                                continue;
                            }
                            expectEquals (plugin->getCurrentProgram(), 2);
                            expect (kind == Kind::effectChain ? controller.isEffectPresetModified()
                                                             : controller.isListeningPresetModified(),
                                    "The current plug-in state differs from the saved snapshot.");
                            ListeningPresetLibrary savedLibrary;
                            expect (ListeningPresetStore::readFromFile (target, savedLibrary, error), error);
                            const auto* effectPreset = savedLibrary.findEffectChain (id);
                            const auto* listeningPreset = savedLibrary.findListening (id);
                            const auto* savedChain = effectPreset != nullptr ? &effectPreset->chain
                                : listeningPreset != nullptr ? &listeningPreset->chain : nullptr;
                            expect (savedChain != nullptr && savedChain->getSlot (0) != nullptr);
                            if (savedChain != nullptr && savedChain->getSlot (0) != nullptr)
                            {
                                const auto& savedState = savedChain->getSlot (0)->serializedState;
                                plugin->setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));
                                expectEquals (plugin->getCurrentProgram(), 1);
                            }
                            expect (controller.savePreset (kind, name, id).wasOk());
                            expect (! (kind == Kind::effectChain ? controller.isEffectPresetModified()
                                                                : controller.isListeningPresetModified()));

                            const auto intact = target.loadFileAsString();
                            expect (target.replaceWithText ("broken primary"));
                            expect (ListeningPresetStore::backupFileFor (target).replaceWithText ("broken backup"));
                            expect (controller.savePreset (kind, name, id).failed());
                            expectEquals (kind == Kind::effectChain ? controller.getActiveEffectPresetId()
                                                                   : controller.getActiveListeningPresetId(), id);
                            expect (! (kind == Kind::effectChain ? controller.isEffectPresetModified()
                                                                : controller.isListeningPresetModified()));
                            plugin->setCurrentProgram (2);
                            plugin->updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
                            expect (controller.savePreset (kind, name, id).failed());
                            expect (kind == Kind::effectChain ? controller.isEffectPresetModified()
                                                             : controller.isListeningPresetModified());
                            expect (target.replaceWithText (intact));
                            expect (ListeningPresetStore::backupFileFor (target).replaceWithText (intact));
                        }
                       #endif

                        beginTest ("saving while playing preserves transport and the captured plug-in program");
                        const auto wave = root.getChildFile ("tone.wav");
                        {
                            std::unique_ptr<juce::OutputStream> stream = wave.createOutputStream();
                            juce::WavAudioFormat format;
                            auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                .withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (16));
                            expect (writer != nullptr);
                            if (writer != nullptr)
                            {
                                juce::AudioBuffer<float> samples (2, 48000 * 2);
                                samples.clear();
                                expect (writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()));
                            }
                        }
                        controller.addFiles ({ wave });
                        expect (controller.setPlaying (true));
                        juce::AudioBuffer<float> block (2, 128);
                        audio->render (block);
                        plugin->setCurrentProgram (1);
                        const auto position = controller.getCurrentPositionSeconds();
                        const auto queue = controller.getPlaylist().getTracks();
                        expect (controller.savePreset (Kind::effectChain, "Program one").wasOk());
                        const auto id = controller.getActiveEffectPresetId();
                        expect (controller.isPlaying());
                        expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.00001);
                        const auto saved = controller.getPresetLibrary().findEffectChain (id);
                        expect (saved != nullptr && saved->chain.getSlot (0)->serializedState.getSize() > 0);
                        plugin->setCurrentProgram (0);
                        complete = false;
                        expect (controller.applyPreset (Kind::effectChain, id, [&] (bool ok, const juce::String& message)
                        { inserted = ok; complete = true; error = message; }).wasOk());
                        expect (dispatchPresetsUntil ([&] { return complete; }));
                        expect (inserted, error);
                        expectEquals (controller.getEffectPluginForEditor (0)->getCurrentProgram(), 1);
                        expect (controller.isPlaying());
                        expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.00001);
                        expectEquals (controller.getPlaylist().getTracks().front().id, queue.front().id);
                        auto* livePlugin = controller.getEffectPluginForEditor (0);
                        complete = false;
                        bool closed = false;
                        expect (controller.applyPreset (Kind::effectChain, id, [&] (bool ok, const juce::String& message)
                        { inserted = ok; complete = true; error = message; }, [&] { closed = true; }).wasOk());
                        expect (controller.setEffectBypassedAtIndex (0, true));
                        expect (dispatchPresetsUntil ([&] { return complete; }));
                        expect (! inserted && ! closed);
                        expect (controller.getEffectPluginForEditor (0) == livePlugin);
                        expect (controller.getEffectChain().getSlot (0)->bypassed);
                        expect (controller.isEffectPresetModified());
                        expect (controller.setEffectBypassedAtIndex (0, false));

                        complete = false;
                        expect (controller.applyPreset (Kind::effectChain, id, [&] (bool ok, const juce::String& message)
                        { inserted = ok; complete = true; error = message; }).wasOk());
                        livePlugin->setCurrentProgram (2);
                        livePlugin->updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
                        expect (dispatchPresetsUntil ([&] { return complete; }));
                        expect (! inserted && controller.getEffectPluginForEditor (0) == livePlugin);
                        expectEquals (livePlugin->getCurrentProgram(), 2);

                        auto unavailable = plugins[0];
                        unavailable.name = "Unavailable fixture";
                        unavailable.fileOrIdentifier = root.getChildFile ("unavailable.vst3").getFullPathName();
                        unavailable.uniqueId = 987654;
                        juce::KnownPluginList catalog;
                        catalog.addType (plugins[0]);
                        catalog.addType (unavailable);
                        StateRestoreResult catalogUpdate;
                        catalogUpdate.pluginCache = catalog.createXml();
                        expect (controller.applyPreparedState (std::move (catalogUpdate)));
                        EffectChainPreset broken;
                        broken.name = "Instantiation failure";
                        broken.chain = controller.getEffectChain();
                        broken.chain.addSlot (unavailable.name, unavailable.createIdentifierString(), "VST3");
                        const auto brokenFile = root.getChildFile ("broken.moonwake-preset");
                        expect (ListeningPresetStore::exportPreset (broken, brokenFile, error), error);
                        expect (controller.importPreset (brokenFile).wasOk());
                        const auto brokenId = controller.getPresetLibrary().getEffectChainPresets().back().id;
                        complete = false; closed = false;
                        expect (controller.applyPreset (Kind::effectChain, brokenId, [&] (bool ok, const juce::String& message)
                        { inserted = ok; complete = true; error = message; }, [&] { closed = true; }).wasOk());
                        expect (dispatchPresetsUntil ([&] { return complete; }));
                        expect (! inserted && ! closed && error.isNotEmpty());
                        expect (controller.getEffectPluginForEditor (0) == livePlugin);
                        expectEquals (controller.getEffectChain().size(), 1);
                        expect (controller.isPlaying());
                        expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.00001);

                        const auto invalid = controller.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine,
                            juce::AudioChannelSet::left, std::numeric_limits<float>::quiet_NaN(), 1000.0 });
                        expect (invalid.failed() && controller.isPlaying());
                        expect (controller.setCalibrationTestSignal ({ CalibrationTestSignal::Kind::sine,
                            juce::AudioChannelSet::left, -30.0f, 1000.0 }).wasOk());
                        expect (! controller.isPlaying());
                        expectWithinAbsoluteError (controller.getCurrentPositionSeconds(), position, 0.00001);
                        controller.stopCalibrationTestSignal();
                        expect (! controller.isPlaying());
                        expect (controller.setPlaying (true));
                        expect (controller.getCalibrationTestSignal().kind == CalibrationTestSignal::Kind::off);
                    }
                }
            }
        }
        expect (root.deleteRecursively());
    }
};
PlayerPresetControllerTests playerPresetControllerTests;
}
