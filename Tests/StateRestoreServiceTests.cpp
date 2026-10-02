#include "app/PlayerController.h"
#include "state/StateRestoreService.h"

#include <juce_events/juce_events.h>

#include <stdexcept>

namespace
{
bool dispatchRestoreUntil (const std::function<bool()>& complete)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while (! complete() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return complete();
}

class RestoreOfflineBackend final : public IAudioDeviceBackend
{
public:
    juce::String initialiseWithDefaultDevices (int, int) override { return "No test device."; }
    juce::StringArray getOutputDeviceNames() const override { return {}; }
    AudioDeviceBackendState getCurrentDeviceState() const override { return {}; }
    juce::AudioDeviceManager::AudioDeviceSetup getAudioDeviceSetup() const override { return {}; }
    juce::String setAudioDeviceSetup (
        const juce::AudioDeviceManager::AudioDeviceSetup&, bool) override { return "No test device."; }
    void addAudioCallback (juce::AudioIODeviceCallback*) override {}
    void removeAudioCallback (juce::AudioIODeviceCallback*) override {}
};

class StateRestoreServiceTests final : public juce::UnitTest
{
public:
    StateRestoreServiceTests() : UnitTest ("StateRestoreService", "state") {}

    void runTest() override
    {
        const juce::File statePath ("/tmp/moonwake-restore-fixture.xml");

        beginTest ("prepares off the message thread while the UI dispatches messages");
        {
            juce::WaitableEvent entered;
            juce::WaitableEvent release;
            std::atomic_bool workerThread { false };
            StateRestoreService service ([&] (const juce::File&, const juce::File&,
                                              const std::atomic_bool*)
            {
                workerThread.store (! juce::MessageManager::getInstance()->isThisTheMessageThread());
                entered.signal();
                release.wait (2000);
                StateRestoreResult result;
                result.state.emplace();
                return result;
            });
            bool completed = false;
            bool busyDuringCompletion = false;
            bool uiDispatched = false;
            expect (service.start (statePath, {}, [&] (StateRestoreResult result)
            {
                completed = true;
                busyDuringCompletion = service.isRunning();
                expect (juce::MessageManager::getInstance()->isThisTheMessageThread());
                expect (result.state.has_value());
            }));
            expect (entered.wait (1000));
            expect (workerThread.load());
            expect (! service.start (statePath, {}, [] (StateRestoreResult) {}));
            juce::MessageManager::callAsync ([&] { uiDispatched = true; });
            expect (dispatchRestoreUntil ([&] { return uiDispatched; }));
            expect (! completed);
            release.signal();
            expect (dispatchRestoreUntil ([&] { return completed; }));
            expect (busyDuringCompletion);
            expect (! service.isRunning());
        }

        beginTest ("cancellation also invalidates a completed result waiting for delivery");
        {
            StateRestoreService service ([] (const juce::File&, const juce::File&,
                                             const std::atomic_bool*)
            {
                StateRestoreResult result;
                result.state.emplace();
                result.pluginCache = std::make_unique<juce::XmlElement> ("KNOWNPLUGINS");
                return result;
            });
            bool completed = false;
            expect (service.start (statePath, {}, [&] (StateRestoreResult result)
            {
                expect (result.cancelled);
                expect (! result.state);
                expect (result.pluginCache == nullptr);
                completed = true;
            }));
            expect (service.waitForPendingRestore (1000));
            expect (! completed);
            service.cancel();
            expect (dispatchRestoreUntil ([&] { return completed; }));
        }

        beginTest ("destroying the service suppresses queued completion");
        {
            bool completed = false;
            {
                StateRestoreService service ([] (const juce::File&, const juce::File&,
                                                 const std::atomic_bool*)
                {
                    StateRestoreResult result;
                    result.state.emplace();
                    return result;
                });
                expect (service.start (statePath, {}, [&] (StateRestoreResult) { completed = true; }));
                expect (service.waitForPendingRestore (1000));
            }
            bool drained = false;
            juce::MessageManager::callAsync ([&] { drained = true; });
            expect (dispatchRestoreUntil ([&] { return drained; }));
            expect (! completed);
        }

        beginTest ("reports preparation exceptions to the completion callback");
        {
            StateRestoreService service ([] (const juce::File&, const juce::File&,
                                             const std::atomic_bool*) -> StateRestoreResult
            {
                throw std::runtime_error ("fixture restore failure");
            });
            bool completed = false;
            expect (service.start (statePath, {}, [&] (StateRestoreResult result)
            {
                expect (! result.state);
                expect (result.errorMessage.contains ("fixture restore failure"));
                completed = true;
            }));
            expect (dispatchRestoreUntil ([&] { return completed; }));
        }

        beginTest ("prepares saved state and preserves a missing selection checkpoint");
        {
            const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getNonexistentChildFile ("moonwake-state-restore", {});
            expect (root.createDirectory());
            const auto stateFile = root.getChildFile ("state.xml");
            const auto plugins = root.getChildFile ("plugins.xml");
            PlayerAppState saved;
            TrackItem missing { root.getChildFile ("missing.wav"), "Saved title", 120.0 };
            missing.id = "missing-id";
            missing.artist = "Saved artist";
            saved.playlist.addTrack (missing);
            saved.playbackPositionSeconds = 47.25;
            saved.activeSkinId = "example.skin";
            juce::String error;
            expect (AppStateStore::writeAtomically (saved, stateFile, error), error);
            expect (plugins.replaceWithText ("<KNOWNPLUGINS/>"));
            auto result = StateRestoreService::prepare (stateFile, plugins, root.getChildFile ("artwork"));
            expect (result.errorMessage.isEmpty(), result.errorMessage);
            expect (result.state.has_value());
            expect (result.pluginCache != nullptr);
            if (result.state)
            {
                expectEquals (result.state->playlist.getSelectedTrack()->id, missing.id);
                expectEquals (result.state->playlist.getSelectedTrack()->artist, missing.artist);
                expect (! result.state->playlist.getSelectedTrack()->available);
                expectWithinAbsoluteError (result.state->playbackPositionSeconds, 47.25, 0.001);
                expectEquals (result.state->activeSkinId, saved.activeSkinId);
            }
            expect (stateFile.replaceWithText ("<MoonwakeState schemaVersion=\"999\"/>"));
            const auto incompatible = StateRestoreService::prepare (stateFile, plugins, {});
            expect (! incompatible.state);
            expect (incompatible.errorMessage.containsIgnoreCase ("newer"));
            expect (root.deleteRecursively());
        }

        beginTest ("an absent state is a normal first launch and cache failures remain visible");
        {
            const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getNonexistentChildFile ("moonwake-first-restore", {});
            expect (root.createDirectory());
            const auto missingState = root.getChildFile ("state.xml");
            const auto plugins = root.getChildFile ("plugins.xml");
            auto absent = StateRestoreService::prepare (missingState, plugins, {});
            expect (! absent.state);
            expect (absent.errorMessage.isEmpty());
            PlayerController controller (std::make_unique<RestoreOfflineBackend>());
            expect (controller.applyPreparedState (std::move (absent)));
            expect (plugins.replaceWithText ("<wrong-root/>"));
            auto cacheFailure = StateRestoreService::prepare (missingState, plugins, {});
            expect (cacheFailure.pluginCache == nullptr);
            expect (! cacheFailure.warnings.isEmpty());
            expect (controller.applyPreparedState (std::move (cacheFailure)));
            expect (controller.getLastStatusMessage().containsIgnoreCase ("cache"));
            expect (root.deleteRecursively());
        }

        beginTest ("applying prepared state preserves metadata and rejects cancelled replacement");
        {
            PlayerController controller (std::make_unique<RestoreOfflineBackend>());
            StateRestoreResult prepared;
            prepared.state.emplace();
            int identityReads = 0;
            prepared.state->playlist = PlaylistModel ([&] (const juce::File& file)
            {
                ++identityReads;
                return file.getFullPathName();
            });
            TrackItem track { juce::File ("/tmp/absent-prepared-track.wav"), "Prepared title", 120.0 };
            track.id = "prepared-id";
            track.available = false;
            prepared.state->playlist.addTrack (track);
            prepared.state->playbackPositionSeconds = 47.25;
            prepared.state->audioDevice.outputDeviceName = "Unavailable output";
            prepared.state->effectChain.addSlot ("Missing effect", "missing-effect-id");
            prepared.warnings.add ("Prepared artwork warning.");
            expect (controller.applyPreparedState (std::move (prepared)));
            expectEquals (identityReads, 1);
            expectEquals (controller.getPlaylist().getSelectedTrack()->id, track.id);
            expectEquals (controller.getPlaylist().getSelectedTrack()->title, track.title);
            expect (dispatchRestoreUntil ([&]
            {
                return controller.getLastStatusMessage().contains ("Saved effect chain restored.");
            }));
            expect (controller.getLastStatusMessage().contains ("Prepared artwork warning."));
            expect (controller.getLastStatusMessage().contains ("Unavailable output"));
            PlayerAppState captured;
            juce::String error;
            expect (controller.captureStateSnapshot (captured, error), error);
            expectWithinAbsoluteError (captured.playbackPositionSeconds, 47.25, 0.001);
            StateRestoreResult cancelled;
            cancelled.state.emplace();
            cancelled.cancelled = true;
            expect (! controller.applyPreparedState (std::move (cancelled)));
            expectEquals (controller.getPlaylist().getSelectedTrack()->id, track.id);
            StateRestoreResult failed;
            failed.errorMessage = "Unreadable session.";
            expect (! controller.applyPreparedState (std::move (failed)));
            expectEquals (controller.getLastStatusMessage(), juce::String ("Unreadable session."));
            expectEquals (controller.getPlaylist().getSelectedTrack()->id, track.id);
        }
    }
};

StateRestoreServiceTests stateRestoreServiceTests;
} // namespace
