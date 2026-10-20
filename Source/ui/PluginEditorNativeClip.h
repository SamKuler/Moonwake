#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

std::unique_ptr<juce::Component> createNativePluginEditorContent (juce::AudioProcessorEditor&);
