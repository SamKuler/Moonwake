#include "TrackMetadataReader.h"

TrackMetadataReader::TrackMetadataReader()
{
    formatManager.registerBasicFormats();
}

TrackMetadataResult TrackMetadataReader::read (const juce::File& file) const
{
    TrackMetadataResult result;
    result.track.file = file;
    result.track.title = file.getFileNameWithoutExtension().isNotEmpty()
                             ? file.getFileNameWithoutExtension()
                             : file.getFileName();
    result.track.available = false;

    if (! file.existsAsFile())
    {
        result.error = "Audio file not found: " + file.getFullPathName();
        return result;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        result.error = "Unsupported or unreadable audio file: " + file.getFileName();
        return result;
    }

    if (reader->sampleRate <= 0.0 || reader->numChannels == 0)
    {
        result.error = "Audio file has invalid stream metadata: " + file.getFileName();
        return result;
    }

    result.track.durationSeconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
    result.track.sampleRate = reader->sampleRate;
    result.track.channelCount = static_cast<int> (reader->numChannels);
    result.track.available = true;
    result.ok = true;
    return result;
}
