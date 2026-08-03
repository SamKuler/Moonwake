#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "plugins/PluginManager.h"
#include "plugins/PluginScanProcess.h"

#include <cmath>

namespace
{
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

class PluginFormatIntegrationTests final : public juce::UnitTest
{
public:
    PluginFormatIntegrationTests()
        : UnitTest ("PluginFormatIntegration", "plugins")
    {
    }

    void runTest() override
    {
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
                    expect (processFixtureAcrossTenChannels (*instance, error), error);
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
                        expect (processAudioUnitAcrossTenChannels (*instance, error), error);
                }
            }
        }
       #endif
    }
};

PluginFormatIntegrationTests pluginFormatIntegrationTests;
}
