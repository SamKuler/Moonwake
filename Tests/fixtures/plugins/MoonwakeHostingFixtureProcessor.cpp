#include "MoonwakeHostingFixtureProcessor.h"

namespace
{
template <typename Sample>
void applyFixtureGain (juce::AudioBuffer<Sample>& buffer, float gain)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.applyGain (channel, 0, buffer.getNumSamples(), static_cast<Sample> (gain));
}
}

MoonwakeHostingFixtureProcessor::MoonwakeHostingFixtureProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

const juce::String MoonwakeHostingFixtureProcessor::getName() const
{
    return JucePlugin_Name;
}

void MoonwakeHostingFixtureProcessor::prepareToPlay (double, int)
{
}

void MoonwakeHostingFixtureProcessor::releaseResources()
{
}

bool MoonwakeHostingFixtureProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output && ! input.isDisabled() && input.size() <= 10;
}

void MoonwakeHostingFixtureProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                     juce::MidiBuffer&)
{
    const auto program = currentProgram.load();
    applyFixtureGain (buffer, program == 0 ? 0.5f : program == 1 ? 0.25f : 0.75f);
}

void MoonwakeHostingFixtureProcessor::processBlock (juce::AudioBuffer<double>& buffer,
                                                     juce::MidiBuffer&)
{
    const auto program = currentProgram.load();
    applyFixtureGain (buffer, program == 0 ? 0.5f : program == 1 ? 0.25f : 0.75f);
}

bool MoonwakeHostingFixtureProcessor::acceptsMidi() const { return false; }
bool MoonwakeHostingFixtureProcessor::producesMidi() const { return false; }
bool MoonwakeHostingFixtureProcessor::isMidiEffect() const { return false; }
double MoonwakeHostingFixtureProcessor::getTailLengthSeconds() const { return 0.0; }
bool MoonwakeHostingFixtureProcessor::hasEditor() const { return false; }
juce::AudioProcessorEditor* MoonwakeHostingFixtureProcessor::createEditor() { return nullptr; }
int MoonwakeHostingFixtureProcessor::getNumPrograms() { return 3; }
int MoonwakeHostingFixtureProcessor::getCurrentProgram() { return currentProgram.load(); }
void MoonwakeHostingFixtureProcessor::setCurrentProgram (int index)
{
    currentProgram.store (juce::jlimit (0, 2, index));
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}
const juce::String MoonwakeHostingFixtureProcessor::getProgramName (int) { return {}; }
void MoonwakeHostingFixtureProcessor::changeProgramName (int, const juce::String&) {}
void MoonwakeHostingFixtureProcessor::getStateInformation (juce::MemoryBlock& state)
{
    const auto value = currentProgram.load();
    state.replaceAll (&value, sizeof (value));
}
void MoonwakeHostingFixtureProcessor::setStateInformation (const void* data, int size)
{
    if (size != static_cast<int> (sizeof (int))) return;
    int value = 0;
    std::memcpy (&value, data, sizeof (value));
    setCurrentProgram (value);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MoonwakeHostingFixtureProcessor();
}
