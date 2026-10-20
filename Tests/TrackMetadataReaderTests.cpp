#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "library/TrackMetadataReader.h"
#include "library/TrackTagReader.h"
#include "library/LibraryProjection.h"
#include "library/LibraryRestore.h"
#include "state/AppStateStore.h"
#include "fixtures/TrackMetadataFixtures.h"

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
        : UnitTest ("TrackMetadataReader", "library")
    {
    }

    void runTest() override
    {
        beginTest ("one tag pass preserves metadata and artists with independent unsupported-frame fallback");
        {
            const auto file = getMetadataTestDirectory().getNonexistentChildFile ("combined-tags", ".tag");
            const auto writeId3 = [&] (bool badAlbum, bool badArtist)
            {
                auto album = trackMetadataFixtures::id3 (4, 3, { "Night / Live" });
                auto artist = trackMetadataFixtures::id3 (4, 3, { "First", "AC/DC" });
                auto* albumBytes = static_cast<unsigned char*> (album.getData());
                auto* artistBytes = static_cast<unsigned char*> (artist.getData());
                std::memcpy (albumBytes + 10, "TALB", 4);
                if (badAlbum) albumBytes[19] = 0x08;
                if (badArtist) artistBytes[19] = 0x08;
                const auto bodySize = album.getSize() + artist.getSize() - 20;
                for (int index = 0; index < 4; ++index)
                    albumBytes[6 + index] = static_cast<unsigned char> ((bodySize >> ((3 - index) * 7)) & 0x7f);
                album.append (artistBytes + 10, artist.getSize() - 10);
                return file.replaceWithData (album.getData(), album.getSize());
            };
            for (const auto badAlbum : { false, true })
                for (const auto badArtist : { false, true })
                {
                    expect (writeId3 (badAlbum, badArtist));
                    const auto tags = readTrackTags (file);
                    expectEquals (tags.metadata["album"], badAlbum ? juce::String {} : juce::String ("Night / Live"));
                    expect (tags.artists == (badArtist ? juce::StringArray {} : juce::StringArray { "First", "AC/DC" }));
                    expect (tags.artists == readTrackArtistTags (file));
                }
            juce::StringPairArray values;
            values.set ("ALBUM", "Night / Live");
            values.set ("REPLAYGAIN_TRACK_GAIN", "-4.5 dB");
            const auto comments = trackMetadataFixtures::vorbisComments ({ "First", "AC/DC" }, values);
            juce::MemoryOutputStream flac;
            flac.write ("fLaC", 4);
            flac.writeByte (static_cast<char> (0x84));
            for (int shift = 2; shift >= 0; --shift)
                flac.writeByte (static_cast<char> ((comments.getSize() >> (shift * 8)) & 0xff));
            flac.write (comments.getData(), comments.getSize());
            expect (file.replaceWithData (flac.getData(), flac.getDataSize()));
            const auto tags = readTrackTags (file);
            expectEquals (tags.metadata["album"], juce::String ("Night / Live"));
            expectEquals (tags.metadata["replaygaintrackgain"], juce::String ("-4.5 dB"));
            expect (tags.artists == juce::StringArray { "First", "AC/DC" });
            expect (file.deleteFile());
        }

        beginTest ("ID3 album text keeps punctuation and Unicode across supported encodings");
        {
            const auto file = getMetadataTestDirectory().getNonexistentChildFile ("album-tags", ".tag");
            for (const auto version : { 3, 4 })
                for (int encoding = 0; encoding <= (version == 3 ? 1 : 3); ++encoding)
                {
                    const auto album = encoding == 0 ? juce::String ("Night / Live") : juce::String::fromUTF8 ("夜航 / Live");
                    auto tag = trackMetadataFixtures::id3 (version, encoding, { album });
                    std::memcpy (static_cast<unsigned char*> (tag.getData()) + 10, "TALB", 4);
                    expect (file.replaceWithData (tag.getData(), tag.getSize()));
                    expectEquals (readTrackMetadataTags (file)["album"], album);
                    tag.setSize (tag.getSize() - 1);
                    expect (file.replaceWithData (tag.getData(), tag.getSize()));
                    expectEquals (readTrackMetadataTags (file).size(), 0);
                }
            expect (file.deleteFile());
        }

        beginTest ("ID3v2.3 collaboration is indexed for each artist and legacy restore updates every occurrence");
        {
            const auto file = getMetadataTestDirectory().getNonexistentChildFile ("collaboration", ".mp3");
            expect (trackMetadataFixtures::writeMp3 (file, 3, 1,
                { juce::String::fromUTF8 ("3LAU/Said The Sky/NÉONHÈART") }));
            TrackMetadataReader reader;
            const auto metadata = reader.read (file, false);
            expect (metadata.ok, metadata.error);
            MusicLibrary library;
            const auto id = library.upsertTrack (metadata.track);
            PlaylistModel queue;
            for (const auto& artist : juce::StringArray { "3LAU", "Said The Sky", juce::String::fromUTF8 ("NÉONHÈART") })
            {
                const auto rows = libraryTracks (library, queue, "artist:" + artist);
                expectEquals (static_cast<int> (rows.size()), 1, artist);
                const auto summary = libraryCollectionSummary (library, queue, "artist:" + artist);
                expectEquals (summary.name, artist);
                expectEquals (summary.trackCount, 1);
            }
            expectEquals (static_cast<int> (libraryCollectionSummaries (library, "artists").size()), 3);
            expectEquals (static_cast<int> (libraryTracks (library, queue, "all").size()), 1);
            SmartPlaylistRules rules { SmartPlaylistMatch::all,
                { { SmartPlaylistField::artist, SmartPlaylistOperator::equals, "said the sky" } } };
            expect (matchesSmartPlaylistRules (rules, *library.getTrack (id), false, 0));
            rules.rules[0].comparison = SmartPlaylistOperator::notEquals;
            expect (! matchesSmartPlaylistRules (rules, *library.getTrack (id), false, 0));

            PlayerAppState legacy;
            TrackItem saved { file, "Fire", 1.0 };
            saved.artist = juce::String::fromUTF8 ("3LAU/Said The Sky/NÉONHÈART");
            const auto savedId = legacy.library.upsertTrack (saved);
            auto occurrence = *legacy.library.getTrack (savedId);
            occurrence.libraryId = savedId;
            occurrence.id = "first-occurrence";
            legacy.playlist.addTrack (occurrence);
            auto restored = AppStateStore::fromValueTree (AppStateStore::toValueTree (legacy));
            juce::StringArray warnings;
            expect (prepareLibraryRestore (restored.library, restored.playlist,
                getMetadataTestDirectory().getChildFile ("artist-restore-cache"), nullptr, warnings));
            expect (warnings.isEmpty(), warnings.joinIntoString ("; "));
            expectEquals (restored.playlist.getTrack (0)->id, occurrence.id);
            expectEquals (restored.playlist.getTrack (0)->libraryId, savedId);
            expectEquals (static_cast<int> (libraryTracks (restored.library, restored.playlist,
                                                          "artist:Said The Sky").size()), 1);
            expect (file.deleteFile());
        }

        beginTest ("ID3v2.4 null-separated values preserve literal punctuation in every text encoding");
        {
            for (const auto encoding : { 0, 1, 2, 3 })
            {
                const auto file = getMetadataTestDirectory().getNonexistentChildFile ("multiple-id3-v4", ".mp3");
                const juce::StringArray expected { "AC/DC", "Earth, Wind & Fire", juce::String::fromUTF8 ("NÉONHÈART") };
                expect (trackMetadataFixtures::writeMp3 (file, 4, encoding, expected));
                TrackMetadataReader reader;
                const auto result = reader.read (file, false);
                expect (result.ok, result.error);
                expect (result.track.artists == expected, "Encoding " + juce::String (encoding));
                for (const auto& artist : expected) expect (result.track.artist.contains (artist));
                expect (file.deleteFile());
            }
            TrackItem flat;
            juce::StringPairArray metadata;
            metadata.set ("ARTIST", "AC/DC, Earth & Friends");
            TrackMetadataReader::applyMetadata (flat, metadata);
            expect (flat.artists == juce::StringArray { "AC/DC, Earth & Friends" });
            expectEquals (flat.artist, juce::String ("AC/DC, Earth & Friends"));
        }

        beginTest ("ID3 artist parsing handles versioned sizes unsynchronisation and extended headers");
        {
            const auto file = getMetadataTestDirectory().getNonexistentChildFile ("id3-bounds", ".tag");
            const juce::StringArray expected { "First", juce::String::fromUTF8 ("NÉONHÈART") };
            for (const auto version : { 3, 4 })
            {
                auto tag = trackMetadataFixtures::id3 (version, 1,
                    version == 3 ? juce::StringArray { expected.joinIntoString ("/") } : expected);
                const auto* source = static_cast<const unsigned char*> (tag.getData());
                juce::MemoryOutputStream payload;
                if (version == 3)
                {
                    payload.writeIntBigEndian (6);
                    payload.writeShort (0);
                    payload.writeInt (0);
                }
                else
                {
                    payload.writeIntBigEndian (6);
                    payload.writeByte (1);
                    payload.writeByte (0);
                }
                payload.write (source + 10, tag.getSize() - 10);
                juce::MemoryBlock withExtended (source, 10);
                withExtended.append (payload.getData(), payload.getDataSize());
                auto* bytes = static_cast<unsigned char*> (withExtended.getData());
                bytes[5] = 0x40;
                for (int index = 6; index < 10; ++index)
                    bytes[index] = static_cast<unsigned char> ((payload.getDataSize() >> ((9 - index) * 7)) & 0x7f);
                expect (file.replaceWithData (withExtended.getData(), withExtended.getSize()));
                expect (readTrackArtistTags (file) == expected);
            }
            auto tag = trackMetadataFixtures::id3 (3, 1, { "First/Second" });
            const auto* source = static_cast<const unsigned char*> (tag.getData());
            juce::MemoryOutputStream unsynchronised;
            unsynchronised.write (source, 10);
            for (size_t index = 10; index < tag.getSize(); ++index)
            {
                unsynchronised.writeByte (static_cast<char> (source[index]));
                if (source[index] == 0xff) unsynchronised.writeByte (0);
            }
            auto storage = unsynchronised.getMemoryBlock();
            auto* bytes = static_cast<unsigned char*> (storage.getData());
            bytes[5] = 0x80;
            for (int index = 6; index < 10; ++index)
                bytes[index] = static_cast<unsigned char> (((storage.getSize() - 10) >> ((9 - index) * 7)) & 0x7f);
            expect (file.replaceWithData (storage.getData(), storage.getSize()));
            expect (readTrackArtistTags (file) == juce::StringArray { "First", "Second" });
            for (size_t length = 0; length < tag.getSize(); ++length)
            {
                expect (file.replaceWithData (tag.getData(), length));
                expect (readTrackArtistTags (file).isEmpty(), "Truncated tag at " + juce::String (length));
            }
            bytes[6] = 0x7f;
            expect (file.replaceWithData (storage.getData(), storage.getSize()));
            expect (readTrackArtistTags (file).isEmpty());
            const unsigned char emptyTag[] { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0 };
            expect (file.replaceWithData (emptyTag, sizeof (emptyTag)));
            expect (readTrackArtistTags (file).isEmpty());
            auto invalid = trackMetadataFixtures::id3 (4, 1, { "X" });
            auto* invalidBytes = static_cast<unsigned char*> (invalid.getData());
            invalidBytes[23] = 0;
            invalidBytes[24] = 0xd8;
            expect (file.replaceWithData (invalid.getData(), invalid.getSize()));
            expect (readTrackArtistTags (file).isEmpty());
            expect (file.deleteFile());
        }

        beginTest ("FLAC and continued Ogg Vorbis comments retain repeated artists without splitting punctuation");
        {
            const juce::StringArray members { "AC/DC", "Earth, Wind & Fire", juce::String::fromUTF8 ("NÉONHÈART"), "AC/DC" };
            const juce::StringArray expected { "AC/DC", "Earth, Wind & Fire", juce::String::fromUTF8 ("NÉONHÈART") };
            const auto comments = trackMetadataFixtures::vorbisComments (members);
            const auto file = getMetadataTestDirectory().getNonexistentChildFile ("vorbis-artists", ".tag");
            juce::MemoryOutputStream flac;
            flac.write ("fLaC", 4);
            flac.writeByte (static_cast<char> (0x84));
            for (int shift = 2; shift >= 0; --shift)
                flac.writeByte (static_cast<char> ((comments.getSize() >> (shift * 8)) & 0xff));
            flac.write (comments.getData(), comments.getSize());
            expect (file.replaceWithData (flac.getData(), flac.getDataSize()));
            expect (readTrackArtistTags (file) == expected);

            auto ogg = trackMetadataFixtures::oggPage (juce::MemoryBlock ("\1vorbis", 7), 0);
            juce::MemoryBlock packet ("\3vorbis", 7);
            packet.append (comments.getData(), comments.getSize());
            const auto page = trackMetadataFixtures::oggPage (packet, 1);
            ogg.append (page.getData(), page.getSize());
            expect (file.replaceWithData (ogg.getData(), ogg.getSize()));
            expect (readTrackArtistTags (file) == expected);

            const auto longName = juce::String::repeatedString ("A", 260);
            const auto longer = trackMetadataFixtures::vorbisComments ({ longName, "Second" });
            packet = juce::MemoryBlock ("\3vorbis", 7);
            packet.append (longer.getData(), longer.getSize());
            ogg = trackMetadataFixtures::oggPage (juce::MemoryBlock ("\1vorbis", 7), 0);
            const auto first = trackMetadataFixtures::oggPage (juce::MemoryBlock (packet.getData(), 255), 1, false, false);
            const auto second = trackMetadataFixtures::oggPage (juce::MemoryBlock (
                static_cast<const char*> (packet.getData()) + 255, packet.getSize() - 255), 2, true);
            ogg.append (first.getData(), first.getSize());
            ogg.append (second.getData(), second.getSize());
            expect (file.replaceWithData (ogg.getData(), ogg.getSize()));
            expect (readTrackArtistTags (file) == juce::StringArray { longName, "Second" });
            expect (file.deleteFile());
        }

        beginTest ("real FLAC and Ogg readers preserve every repeated artist through projection");
        {
            for (const auto flac : { true, false })
            {
                const auto file = getMetadataTestDirectory().getNonexistentChildFile (
                    "repeated-artists", flac ? ".flac" : ".ogg");
                auto output = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
                std::unique_ptr<juce::AudioFormat> format;
                if (flac) format = std::make_unique<juce::FlacAudioFormat>();
                else format = std::make_unique<juce::OggVorbisAudioFormat>();
                auto writer = format->createWriterFor (output, juce::AudioFormatWriterOptions{}
                    .withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (16));
                expect (writer != nullptr);
                if (writer != nullptr)
                {
                    juce::AudioBuffer<float> samples (2, 4410);
                    samples.clear();
                    expect (writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()));
                    writer.reset();
                    const juce::StringArray artists { "AC/DC", "Earth, Wind & Fire", juce::String::fromUTF8 ("NÉONHÈART") };
                    juce::StringPairArray tags;
                    tags.set ("TITLE", "Song / Part One");
                    tags.set ("ALBUM", juce::String::fromUTF8 ("夜航 / Live"));
                    tags.set ("ALBUM_ARTIST", "Various Artists");
                    tags.set ("DATE", "2026-10-11");
                    tags.set ("TRACKNUMBER", "4/12");
                    tags.set ("DISCNUMBER", "2/3");
                    expect (trackMetadataFixtures::replaceVorbisArtists (file, flac, artists, tags));
                    TrackMetadataReader reader;
                    const auto read = reader.read (file, false);
                    expect (read.ok, read.error);
                    expectEquals (read.track.title, juce::String ("Song / Part One"));
                    expectEquals (read.track.album, juce::String::fromUTF8 ("夜航 / Live"));
                    expectEquals (read.track.albumArtist, juce::String ("Various Artists"));
                    expectEquals (read.track.year, 2026);
                    expectEquals (read.track.trackNumber, 4);
                    expectEquals (read.track.discNumber, 2);
                    expect (read.track.artists == artists, flac ? "FLAC" : "Ogg Vorbis");
                    expectWithinAbsoluteError (read.track.durationSeconds, 0.1, 0.001);
                    MusicLibrary library;
                    library.upsertTrack (read.track);
                    const auto cards = libraryCollectionSummaries (library, "artists");
                    expectEquals (static_cast<int> (cards.size()), 3);
                    const auto albums = libraryCollectionSummaries (library, "albums");
                    expectEquals (static_cast<int> (albums.size()), 1);
                    if (! albums.empty()) expectEquals (albums.front().name, read.track.album);
                }
                expect (file.deleteFile());
            }
        }

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
            expect (result.track.fileIdentity.isNotEmpty());
            expectEquals (result.track.fileSizeBytes, wavFile.getSize());
            expectEquals (result.track.fileModifiedAtMs, wavFile.getLastModificationTime().toMilliseconds());
            expectEquals (result.track.addedAtMs, juce::int64 { 0 });

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

        beginTest ("reads real ID3 tags and embedded MP3 artwork on every platform");
        {
            const auto mp3File = getMetadataTestDirectory()
                                     .getNonexistentChildFile ("tagged-with-artwork", ".mp3");
            const auto fixture = trackMetadataFixtures::mp3();
            expect (mp3File.replaceWithData (fixture.getData(), fixture.getSize()));

            TrackMetadataReader metadataReader;
            const auto result = metadataReader.read (mp3File);
            expect (result.ok, result.error);
            expectEquals (result.track.title, juce::String ("Dawn Current"));
            expectEquals (result.track.artist, juce::String ("Sam Kuler"));
            expectEquals (result.track.album, juce::String ("Natural Works"));
            expectEquals (result.track.albumArtist, juce::String ("Moonwake Ensemble"));
            expectEquals (result.track.discNumber, 2);
            expectEquals (result.track.genre, juce::String ("Ambient"));
            expectEquals (result.track.year, 2026);
            expectEquals (result.track.trackNumber, 4);
            expectEquals (static_cast<int> (result.artworkData.getSize()), 247);

            expect (mp3File.deleteFile());
        }
    }
};

TrackMetadataReaderTests trackMetadataReaderTests;
} // namespace
