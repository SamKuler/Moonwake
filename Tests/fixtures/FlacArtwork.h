#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace flacArtworkFixture
{
inline juce::MemoryBlock picture (const juce::MemoryBlock& image, int type = 3,
                                  const juce::String& mime = "image/png",
                                  const juce::String& description = {})
{
    juce::MemoryOutputStream output;
    output.writeIntBigEndian (type);
    output.writeIntBigEndian (static_cast<int> (mime.getNumBytesAsUTF8()));
    output.write (mime.toRawUTF8(), mime.getNumBytesAsUTF8());
    output.writeIntBigEndian (static_cast<int> (description.getNumBytesAsUTF8()));
    output.write (description.toRawUTF8(), description.getNumBytesAsUTF8());
    for (int index = 0; index < 4; ++index) output.writeIntBigEndian (0);
    output.writeIntBigEndian (static_cast<int> (image.getSize()));
    output.write (image.getData(), image.getSize());
    return output.getMemoryBlock();
}

inline bool write (const juce::File& file, const std::vector<juce::MemoryBlock>& pictures)
{
    {
        auto output = file.createOutputStream();
        if (! output || ! output->setPosition (0) || output->truncate().failed()) return false;
        std::unique_ptr<juce::OutputStream> stream = std::move (output);
        juce::FlacAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
            .withSampleRate (48000).withNumChannels (2).withBitsPerSample (16));
        if (! writer) return false;
        juce::AudioBuffer<float> samples (2, 4800);
        samples.clear();
        if (! writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples())) return false;
    }
    juce::MemoryBlock original;
    if (! file.loadFileAsData (original) || original.getSize() < 42) return false;
    auto* bytes = static_cast<unsigned char*> (original.getData());
    const auto streamInfoWasLast = (bytes[4] & 0x80) != 0;
    if (! pictures.empty()) bytes[4] &= 0x7f;
    juce::MemoryOutputStream output;
    output.write (bytes, 42); // Signature, STREAMINFO header and its 34-byte payload.
    for (size_t index = 0; index < pictures.size(); ++index)
    {
        const auto& data = pictures[index];
        output.writeByte (static_cast<char> (6 | (streamInfoWasLast && index + 1 == pictures.size() ? 0x80 : 0)));
        for (int shift = 2; shift >= 0; --shift)
            output.writeByte (static_cast<char> ((data.getSize() >> (shift * 8)) & 0xff));
        output.write (data.getData(), data.getSize());
    }
    output.write (bytes + 42, original.getSize() - 42);
    return file.replaceWithData (output.getData(), output.getDataSize());
}
}
