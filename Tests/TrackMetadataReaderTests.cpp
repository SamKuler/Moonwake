#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "audio/TrackMetadataReader.h"

namespace
{
juce::File getMetadataTestDirectory()
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
                               .getChildFile ("build")
                               .getChildFile ("track-metadata-tests");
    static_cast<void> (directory.createDirectory());
    return directory;
}

class TrackMetadataReaderTests final : public juce::UnitTest
{
public:
    TrackMetadataReaderTests()
        : UnitTest ("TrackMetadataReader", "audio")
    {
    }

    void runTest() override
    {
        beginTest ("normalises common metadata aliases without replacing useful values");
        {
            TrackItem track { juce::File ("/Music/file-name.wav"), "file-name", 0.0 };
            juce::StringPairArray metadata;
            metadata.set ("ID3 title", "  First Light  ");
            metadata.set ("Artist", "Sam\nKuler");
            metadata.set ("album_artist", "Moonwake Ensemble");
            metadata.set ("Album", "Natural Works");
            metadata.set ("Genre", "Ambient");
            metadata.set ("Date", "2026-08-07");
            metadata.set ("Track Number", "03/12");
            metadata.set ("Disc Number", "2/2");

            TrackMetadataReader::applyMetadata (track, metadata);

            expectEquals (track.title, juce::String ("First Light"));
            expectEquals (track.artist, juce::String ("Sam Kuler"));
            expectEquals (track.albumArtist, juce::String ("Moonwake Ensemble"));
            expectEquals (track.album, juce::String ("Natural Works"));
            expectEquals (track.genre, juce::String ("Ambient"));
            expectEquals (track.year, 2026);
            expectEquals (track.trackNumber, 3);
            expectEquals (track.discNumber, 2);
        }

        beginTest ("metadata normalisation is case-insensitive and keeps filename fallbacks");
        {
            TrackItem track { juce::File ("/Music/untitled.flac"), "untitled", 0.0 };
            juce::StringPairArray metadata;
            metadata.set ("kAFInfoDictionary_ARTIST", "  Artist Name  ");
            metadata.set ("ID3ALBUM", "Album Name");
            metadata.set ("TITLE", "   ");
            metadata.set ("YEAR", "not-a-year");
            metadata.set ("TRACK", "zero");

            TrackMetadataReader::applyMetadata (track, metadata);

            expectEquals (track.title, juce::String ("untitled"));
            expectEquals (track.artist, juce::String ("Artist Name"));
            expectEquals (track.album, juce::String ("Album Name"));
            expectEquals (track.year, 0);
            expectEquals (track.trackNumber, 0);
        }

        beginTest ("reads duration sample rate and channel count from a WAV file");
        {
            const auto wavFile = getMetadataTestDirectory().getNonexistentChildFile ("one-second-stereo", ".wav");
            auto output = std::unique_ptr<juce::OutputStream> (wavFile.createOutputStream());
            expect (output != nullptr, "Could not create WAV output at " + wavFile.getFullPathName());
            juce::WavAudioFormat wavFormat;
            const auto options = juce::AudioFormatWriterOptions{}
                                     .withSampleRate (44100.0)
                                     .withNumChannels (2)
                                     .withBitsPerSample (16)
                                     .withMetadata (juce::WavAudioFormat::riffInfoTitle,
                                                    "Dawn Current")
                                     .withMetadata (juce::WavAudioFormat::riffInfoArtist,
                                                    "Sam Kuler")
                                     .withMetadata (juce::WavAudioFormat::riffInfoProductName,
                                                    "Natural Works")
                                     .withMetadata (juce::WavAudioFormat::riffInfoTrackNumber,
                                                    "4/9")
                                     .withMetadata (juce::WavAudioFormat::riffInfoYear,
                                                    "2026");
            auto writer = wavFormat.createWriterFor (output, options);
            expect (writer != nullptr);

            if (writer != nullptr)
            {
                juce::AudioBuffer<float> samples (2, 44100);
                samples.clear();
                expect (writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()));
                writer.reset();
            }

            TrackMetadataReader metadataReader;
            const auto result = metadataReader.read (wavFile);
            expect (result.ok);
            expectWithinAbsoluteError (result.track.durationSeconds, 1.0, 0.001);
            expectWithinAbsoluteError (result.track.sampleRate, 44100.0, 0.001);
            expectEquals (result.track.channelCount, 2);
            expectEquals (result.track.bitDepth, 16);
            expect (result.track.formatName.isNotEmpty());
            expectEquals (result.track.title, juce::String ("Dawn Current"));
            expectEquals (result.track.artist, juce::String ("Sam Kuler"));
            expectEquals (result.track.album, juce::String ("Natural Works"));
            expectEquals (result.track.trackNumber, 4);
            expectEquals (result.track.year, 2026);
            expect (result.track.available);
            expect (result.track.id.isNotEmpty());

            expect (wavFile.deleteFile());
        }

#if JUCE_MAC
        beginTest ("reads metadata from a long non-ASCII macOS path safely");
        {
            const auto wavFile = getMetadataTestDirectory().getNonexistentChildFile (
                juce::String::fromUTF8 (
                    "月光-阿姆斯特朗-非常长的音频文件名用于验证路径生命周期-"
                    "abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz"),
                ".wav");
            auto output = std::unique_ptr<juce::OutputStream> (
                wavFile.createOutputStream());
            expect (output != nullptr,
                    "Could not create Unicode WAV at "
                        + wavFile.getFullPathName());

            juce::WavAudioFormat wavFormat;
            auto writer = wavFormat.createWriterFor (
                output,
                juce::AudioFormatWriterOptions{}
                    .withSampleRate (48000.0)
                    .withNumChannels (2)
                    .withBitsPerSample (24)
                    .withMetadata (juce::WavAudioFormat::riffInfoTitle,
                                   "Moonlit Path"));
            expect (writer != nullptr);
            if (writer != nullptr)
            {
                juce::AudioBuffer<float> samples (2, 128);
                samples.clear();
                expect (writer->writeFromAudioSampleBuffer (
                    samples, 0, samples.getNumSamples()));
                writer.reset();
            }

            TrackMetadataReader metadataReader;
            const auto result = metadataReader.read (wavFile, false);
            expect (result.ok, result.error);
            expectEquals (result.track.title,
                          juce::String ("Moonlit Path"));
            expectEquals (result.track.file.getFullPathName(),
                          wavFile.getFullPathName());
            expect (wavFile.deleteFile());
        }
#endif

        beginTest ("rejects files that no registered audio format can read");
        {
            const auto textFile = getMetadataTestDirectory().getNonexistentChildFile ("not-audio", ".txt");
            expect (textFile.replaceWithText ("not audio"));

            TrackMetadataReader metadataReader;
            const auto result = metadataReader.read (textFile);
            expect (! result.ok);
            expect (result.error.isNotEmpty());
            expectEquals (result.track.file.getFullPathName(), textFile.getFullPathName());
            expect (! result.track.available);

            expect (textFile.deleteFile());
        }

        beginTest ("reports missing files as unavailable");
        {
            const auto missingFile = getMetadataTestDirectory().getNonexistentChildFile ("missing", ".wav");

            TrackMetadataReader metadataReader;
            const auto result = metadataReader.read (missingFile);
            expect (! result.ok);
            expect (! result.track.available);
            expect (result.error.containsIgnoreCase ("not found"));
        }

#if JUCE_MAC
        beginTest ("reads real ID3 tags and embedded MP3 artwork on macOS");
        {
            const auto mp3File = getMetadataTestDirectory()
                                     .getNonexistentChildFile ("tagged-with-artwork", ".mp3");
            juce::MemoryBlock fixture;
            juce::MemoryOutputStream fixtureStream (fixture, false);
            expect (juce::Base64::convertFromBase64 (fixtureStream,
                "SUQzAwAAAAADYVRJVDIAAAAOAAAARGF3biBDdXJyZW50AFRQRTEAAAALAAAAU2FtIEt1bGVy"
                "AFRBTEIAAAAPAAAATmF0dXJhbCBXb3JrcwBUUEUyAAAAEwAAAE1vb253YWtlIEVuc2VtYmxl"
                "AFRDT04AAAAJAAAAQW1iaWVudABUWUVSAAAABgAAADIwMjYAVFJDSwAAAAUAAAA0LzkAVFBP"
                "UwAAAAUAAAAyLzIAVFNTRQAAAA8AAABMYXZmNjIuMTIuMTAyAEFQSUMAAAEQAAAAaW1hZ2Uv"
                "anBlZwADQWxidW0gY292ZXIA/9j/4AAQSkZJRgABAgAAAQABAAD//gAQTGF2YzYyLjI4LjEw"
                "MgD/2wBDAAgEBAQEBAUFBQUFBQYGBgYGBgYGBgYGBgYHBwcICAgHBwcGBgcHCAgICAkJCQgI"
                "CAgJCQoKCgwMCwsODg4RERT/xABMAAEBAAAAAAAAAAAAAAAAAAAABAEBAQAAAAAAAAAAAAAA"
                "AAAAAAYQAQAAAAAAAAAAAAAAAAAAAAARAQAAAAAAAAAAAAAAAAAAAAD/wAARCABAAEADASIA"
                "AhEAAxEA/9oADAMBAAIRAxEAPwCIBNrIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB//2QAAAAAA"
                "AAAAAAD/+1AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABJbmZvAAAADwAAAAsA"
                "AAVIADs7Ozs7Ozs7O05OTk5OTk5OTmJiYmJiYmJiYnZ2dnZ2dnZ2domJiYmJiYmJiZ2dnZ2d"
                "nZ2dnbGxsbGxsbGxscTExMTExMTExNjY2NjY2NjY2Ozs7Ozs7Ozs7P///////////wAAAABM"
                "YXZjNjIuMjgAAAAAAAAAAAAAAAAkBC8AAAAAAAAFSNGQBj4AAAAAAAAAAAAAAAAAAAAA//sQ"
                "ZAAP8AAAaQAAAAgAAA0gAAABAAABpAAAACAAADSAAAAETEFNRTQuMFVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVTEFNRTQuMFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVX/+xBkIg/w"
                "AABpAAAACAAADSAAAAEAAAGkAAAAIAAANIAAAARVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVMQU1FNC4wVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVf/7EGRED/AAAGkA"
                "AAAIAAANIAAAAQAAAaQAAAAgAAA0gAAABFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVUxBTUU0LjBVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV//sQZGYP8AAAaQAAAAgA"
                "AA0gAAABAAABpAAAACAAADSAAAAEVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "TEFNRTQuMFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVX/+xBkiA/wAABpAAAACAAADSAA"
                "AAEAAAGkAAAAIAAANIAAAARVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVf/7EGSqD/AAAGkAAAAIAAANIAAAAQAA"
                "AaQAAAAgAAA0gAAABFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV//sQZMwP8AAAaQAAAAgAAA0gAAABAAABpAAA"
                "ACAAADSAAAAEVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVX/+xBk3Y/wAABpAAAACAAADSAAAAEAAAGkAAAAIAAA"
                "NIAAAARVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVf/7EGTdj/AAAGkAAAAIAAANIAAAAQAAAaQAAAAgAAA0gAAA"
                "BFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVV//sQZN2P8AAAaQAAAAgAAA0gAAABAAABpAAAACAAADSAAAAEVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVVVVVVX/+xBk3Y/wAABpAAAACAAADSAAAAEAAAGkAAAAIAAANIAAAARVVVVVVVVV"
                "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"
                "VVVVVVVVVQ=="));
            fixtureStream.flush();
            expect (mp3File.replaceWithData (fixture.getData(), fixture.getSize()));

            TrackMetadataReader metadataReader;
            const auto result = metadataReader.read (mp3File);
            expect (result.ok, result.error);
            expectEquals (result.track.title, juce::String ("Dawn Current"));
            expectEquals (result.track.artist, juce::String ("Sam Kuler"));
            expectEquals (result.track.album, juce::String ("Natural Works"));
            expectEquals (result.track.genre, juce::String ("Ambient"));
            expectEquals (result.track.year, 2026);
            expectEquals (result.track.trackNumber, 4);
            expectEquals (static_cast<int> (result.artworkData.getSize()), 247);

            expect (mp3File.deleteFile());
        }
#endif
    }
};

TrackMetadataReaderTests trackMetadataReaderTests;
} // namespace
