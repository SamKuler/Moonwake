#include "app/PlayerController.h"
#include "fixtures/LibraryAudioBackend.h"

#include <stdexcept>
#include <thread>

namespace
{
class ControllerTestPlugin final : public juce::AudioPluginInstance
{
public:
    explicit ControllerTestPlugin (bool failsPreparationToUse = false)
        : AudioPluginInstance (BusesProperties()
              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          failsPreparation (failsPreparationToUse)
    {
        juce::AudioProcessor::addParameter (bypass = new juce::AudioParameterBool ("bypass", "Bypass", false));
    }
    const juce::String getName() const override { return "Controller test effect"; }
    void fillInPluginDescription (juce::PluginDescription& description) const override
    {
        description.name = getName();
        description.pluginFormatName = "VST3";
    }
    void prepareToPlay (double, int) override
    {
        if (failsPreparation) throw std::runtime_error ("fixture preparation failure");
    }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        return layout.getMainInputChannelSet() == layout.getMainOutputChannelSet()
            && ! layout.getMainInputChannelSet().isDisabled();
    }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (! bypass->get()) buffer.applyGain (0.5f);
    }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& state) override
    {
        const int value = bypass->get() ? 1 : 0;
        state.replaceAll (&value, sizeof (value));
    }
    void setStateInformation (const void* data, int size) override
    {
        if (size != static_cast<int> (sizeof (int))) return;
        int value = 0;
        std::memcpy (&value, data, sizeof (value));
        if (value < 0) throw std::runtime_error ("fixture state restore failure");
        bypass->setValueNotifyingHost (value != 0 ? 1.0f : 0.0f);
    }
private:
    juce::AudioParameterBool* bypass = nullptr;
    bool failsPreparation = false;
};

class ControllerTestFormat final : public juce::AudioPluginFormat
{
public:
    juce::String getName() const override { return "VST3"; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override {}
    bool fileMightContainThisPluginType (const juce::String&) override { return true; }
    juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return id; }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }
protected:
    void createPluginInstance (const juce::PluginDescription& description, double, int,
                               PluginCreationCallback callback) override
    {
        callback (std::make_unique<ControllerTestPlugin> (description.name == "prepare failure"), {});
    }
};

bool dispatchEffectsUntil (const std::function<bool()>& done)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (! done() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    return done();
}
}

class PlayerEffectControllerTests final : public juce::UnitTest
{
public:
    PlayerEffectControllerTests() : UnitTest ("PlayerEffectController", "app") {}
    void runTest() override
    {
        beginTest ("async insertion failure completes once and preserves the existing graph and model");
        for (const auto layout : { OutputLayout::stereo, OutputLayout::fiveOne,
                                  OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo })
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            const auto plugins = installTestFormat (controller);
            expect (controller.setOutputLayout (layout).isEmpty());
            expect (insert (controller, plugins[0]));
            const auto revision = controller.getEffectChainRevision();
            const auto slotId = controller.getEffectChain().getSlot (0)->id;
            int completions = 0;
            bool accepted = true;
            juce::String error;
            controller.addPluginByIdentifierAsync (plugins[1].createIdentifierString(),
                [&] (bool ok, const juce::String& message)
                {
                    ++completions;
                    accepted = ok;
                    error = message;
                });
            expect (dispatchEffectsUntil ([&] { return completions != 0; }));
            expectEquals (completions, 1);
            expect (! accepted);
            expect (error.isNotEmpty());
            expectEquals (controller.getLastStatusMessage(), error);
            expectEquals (controller.getEffectChain().size(), 1);
            expectEquals (controller.audioEngine.getEffectPluginCount(), 1);
            expectEquals (controller.getEffectChain().getSlot (0)->id, slotId);
            expectEquals (controller.getEffectChainRevision(), revision);
            expect (insert (controller, plugins[0]));
            expect (controller.moveEffect (0, 1));
            expect (controller.removeEffectAtIndex (1));
            expectEquals (controller.audioEngine.getEffectPluginCount(), 1);
        }

        beginTest ("session restore records preparation and state errors and continues to later effects");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            const auto plugins = installTestFormat (controller);
            StateRestoreResult restore;
            restore.state.emplace();
            auto& chain = restore.state->effectChain;
            chain.addSlot (plugins[1].name, plugins[1].createIdentifierString(), "VST3");
            const auto brokenStateId = chain.addSlot (plugins[0].name, plugins[0].createIdentifierString(), "VST3");
            const int rejectedState = -1;
            chain.getSlotById (brokenStateId)->serializedState.replaceAll (&rejectedState, sizeof (rejectedState));
            chain.addSlot (plugins[0].name, plugins[0].createIdentifierString(), "VST3");
            expect (controller.applyPreparedState (std::move (restore)));
            expect (dispatchEffectsUntil ([&] { return ! controller.isPresetApplyRunning(); }));
            expectEquals (controller.getEffectChain().size(), 3);
            expectEquals (controller.audioEngine.getEffectPluginCount(), 1);
            expect (! controller.getEffectChain().getSlot (0)->available);
            expect (! controller.getEffectChain().getSlot (1)->available);
            expect (controller.getEffectChain().getSlot (2)->available);
            expect (controller.getEffectChain().getSlot (0)->errorMessage.contains ("preparing"));
            expect (controller.getEffectChain().getSlot (1)->errorMessage.contains ("fixture state restore failure"));
            expectEquals (controller.getRestoreDiagnostics().size(), 2);
            expect (controller.getLastStatusMessage().contains ("fixture state restore failure"));
            expect (insert (controller, plugins[0]));
        }

        beginTest ("native bypass notifications synchronize on the control thread and survive snapshots and presets");
        {
            PlayerController controller (std::make_unique<LibraryAudioBackend>());
            const auto plugins = installTestFormat (controller);
            expect (insert (controller, plugins[0]));
            auto* plugin = controller.getEffectPluginForEditor (0);
            if (plugin == nullptr) return;
            const auto revision = controller.getEffectChainRevision();
            std::thread notification ([&] { plugin->getBypassParameter()->setValueNotifyingHost (1.0f); });
            notification.join();
            expect (! controller.getEffectChain().getSlot (0)->bypassed,
                    "The audio-thread notification must leave model writes to the control thread.");
            controller.synchronizePlayback();
            expect (controller.getEffectChain().getSlot (0)->bypassed);
            expect (controller.getEffectChainRevision() > revision);
            PlayerAppState snapshot;
            juce::String error;
            expect (controller.captureStateSnapshot (snapshot, error), error);
            expect (snapshot.effectChain.getSlot (0)->bypassed);
            int savedBypass = 0;
            const auto& state = snapshot.effectChain.getSlot (0)->serializedState;
            if (state.getSize() == sizeof (savedBypass))
                std::memcpy (&savedBypass, state.getData(), sizeof (savedBypass));
            expectEquals (savedBypass, 1);

            PlayerController restored (std::make_unique<LibraryAudioBackend>());
            installTestFormat (restored);
            StateRestoreResult restore;
            restore.state = snapshot;
            expect (restored.applyPreparedState (std::move (restore)));
            expect (dispatchEffectsUntil ([&] { return ! restored.isPresetApplyRunning(); }));
            auto* live = restored.getEffectPluginForEditor (0);
            expect (live != nullptr);
            if (live != nullptr)
                expectWithinAbsoluteError (live->getBypassParameter()->getValue(), 1.0f, 0.0001f);
            expect (restored.getEffectChain().getSlot (0)->bypassed);

            const auto presetFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getNonexistentChildFile ("moonwake-native-bypass", ".xml");
            controller.presetFile = presetFile;
            plugin->getBypassParameter()->setValueNotifyingHost (0.0f);
            expect (controller.savePreset (PlayerController::PresetKind::effectChain, "Native bypass").wasOk());
            const auto* preset = controller.getPresetLibrary().findEffectChain (controller.getActiveEffectPresetId());
            expect (preset != nullptr && ! preset->chain.getSlot (0)->bypassed);
            expect (! controller.getEffectChain().getSlot (0)->bypassed);
            expect (! controller.isEffectPresetModified(), "A captured native bypass edit belongs to the saved baseline.");
            plugin->getBypassParameter()->setValueNotifyingHost (1.0f);
            bool applied = false, complete = false;
            expect (controller.applyPreset (PlayerController::PresetKind::effectChain,
                controller.getActiveEffectPresetId(), [&] (bool ok, const juce::String& message)
                { applied = ok; complete = true; error = message; }).wasOk());
            expect (dispatchEffectsUntil ([&] { return complete; }));
            expect (applied, error);
            expect (! controller.getEffectChain().getSlot (0)->bypassed);
            if (auto* appliedPlugin = controller.getEffectPluginForEditor (0))
                expectWithinAbsoluteError (appliedPlugin->getBypassParameter()->getValue(), 0.0f, 0.0001f);
            else expect (false, "The preset must retain its live effect.");
            presetFile.deleteFile();
        }
    }
private:
    juce::Array<juce::PluginDescription> installTestFormat (PlayerController& controller)
    {
        auto& formats = controller.pluginManager.getFormatManager();
        // Each controller owns this idle manager; replace real binary formats so
        // exceptions stay inside C++ and exercise the asynchronous host boundary.
        std::destroy_at (&formats);
        std::construct_at (&formats);
        formats.addFormat (std::make_unique<ControllerTestFormat>());
        juce::Array<juce::PluginDescription> plugins;
        for (const auto* name : { "working", "prepare failure" })
        {
            juce::PluginDescription description;
            description.name = name;
            description.pluginFormatName = "VST3";
            description.fileOrIdentifier = name;
            description.uniqueId = plugins.size() + 1;
            description.numInputChannels = description.numOutputChannels = 2;
            controller.pluginManager.getKnownPluginList().addType (description);
            plugins.add (description);
        }
        return plugins;
    }
    bool insert (PlayerController& controller, const juce::PluginDescription& plugin)
    {
        bool completed = false, accepted = false;
        juce::String error;
        controller.addPluginByIdentifierAsync (plugin.createIdentifierString(),
            [&] (bool ok, const juce::String& message)
            { completed = true; accepted = ok; error = message; });
        expect (dispatchEffectsUntil ([&] { return completed; }));
        expect (accepted, error);
        return accepted;
    }
};

static PlayerEffectControllerTests playerEffectControllerTests;
