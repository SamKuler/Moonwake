#include "PluginScannerWorker.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace
{
std::unique_ptr<juce::AudioPluginFormat> createFormat (
    const juce::String& name)
{
    if (name == "VST3")
        return std::make_unique<juce::VST3PluginFormat>();

   #if JUCE_MAC && JUCE_PLUGINHOST_AU
    if (name == "AudioUnit")
        return std::make_unique<juce::AudioUnitPluginFormat>();
   #endif

    return {};
}
} // namespace

int runPluginScannerWorker (const juce::StringArray& arguments)
{
    if (arguments.size() != 4 || arguments[0] != "--scan")
        return 2;

    auto format = createFormat (arguments[1]);
    if (format == nullptr)
        return 3;

    juce::OwnedArray<juce::PluginDescription> descriptions;
    try
    {
        format->findAllTypesForFile (descriptions, arguments[2]);
    }
    catch (...)
    {
        return 4;
    }

    juce::KnownPluginList known;
    for (const auto* description : descriptions)
        if (description != nullptr)
            known.addType (*description);

    const auto output = juce::File (arguments[3]);
    const auto parent = output.getParentDirectory();
    if ((! parent.isDirectory() && ! parent.createDirectory())
        || output.isDirectory())
        return 5;

    const auto xml = known.createXml();
    if (xml == nullptr)
        return 6;

    juce::TemporaryFile temporary (output);
    if (! xml->writeTo (temporary.getFile())
        || ! temporary.overwriteTargetFileWithTemporary())
        return 7;

    return 0;
}
