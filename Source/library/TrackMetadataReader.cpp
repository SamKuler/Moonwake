#include "TrackMetadataReader.h"
#include "TrackTagReader.h"

#include "library/ArtworkExtractor.h"
#include "playlist/TrackFileIdentity.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <vector>

#if JUCE_MAC
 #include "MacEmbeddedArtworkReader.h"
 #include <AudioToolbox/AudioToolbox.h>
#endif

namespace
{
juce::String normaliseKey (juce::String key)
{
    return key.trim().toLowerCase().removeCharacters (" _-./\\:()[]{}");
}

juce::String sanitiseMetadataText (juce::String value)
{
    value = value.substring (0, 1024).trim()
                 .replaceCharacters ("\r\n\t", "   ");
    while (value.contains ("  "))
        value = value.replace ("  ", " ");
    return value;
}

juce::String valueForAliases (const juce::StringPairArray& metadata,
                              std::initializer_list<const char*> aliases)
{
    const auto keys = metadata.getAllKeys();
    const auto values = metadata.getAllValues();
    for (const auto* alias : aliases)
    {
        const auto canonicalAlias = normaliseKey (alias);
        for (int index = 0; index < keys.size(); ++index)
            if (normaliseKey (keys[index]) == canonicalAlias)
            {
                const auto value = sanitiseMetadataText (values[index]);
                if (value.isNotEmpty())
                    return value;
            }
    }
    return {};
}

int positiveLeadingInteger (const juce::String& value)
{
    const auto trimmed = value.trim();
    int end = 0;
    while (end < trimmed.length() && juce::CharacterFunctions::isDigit (trimmed[end]))
        ++end;
    if (end == 0)
        return 0;
    return juce::jmax (0, trimmed.substring (0, end).getIntValue());
}

#if JUCE_MAC
struct MacAudioMetadata
{
    juce::StringPairArray values;
    juce::StringArray artists;
    juce::MemoryBlock artwork;
    int sourceBitDepth = 0;
};

juce::String stringFromCFString (CFStringRef value)
{
    if (value == nullptr)
        return {};

    const auto maximumBytes = CFStringGetMaximumSizeForEncoding (
        CFStringGetLength (value), kCFStringEncodingUTF8);
    constexpr CFIndex maximumMetadataBytes = 64 * 1024;
    if (maximumBytes < 0 || maximumBytes > maximumMetadataBytes)
        return {};

    std::vector<char> buffer (static_cast<size_t> (maximumBytes) + 1u, '\0');
    if (! CFStringGetCString (value, buffer.data(),
                              static_cast<CFIndex> (buffer.size()),
                              kCFStringEncodingUTF8))
        return {};
    return juce::String::fromUTF8 (buffer.data());
}

juce::String stringFromCFValue (CFTypeRef value)
{
    if (value == nullptr)
        return {};
    if (CFGetTypeID (value) == CFStringGetTypeID())
        return stringFromCFString (static_cast<CFStringRef> (value));
    if (CFGetTypeID (value) == CFNumberGetTypeID())
    {
        if (CFNumberIsFloatType (static_cast<CFNumberRef> (value)))
        {
            double number = 0.0;
            if (CFNumberGetValue (static_cast<CFNumberRef> (value),
                                  kCFNumberDoubleType, &number))
                return juce::String (number, 6).trimCharactersAtEnd ("0")
                                              .trimCharactersAtEnd (".");
        }
        else
        {
            int64_t number = 0;
            if (CFNumberGetValue (static_cast<CFNumberRef> (value),
                                  kCFNumberSInt64Type, &number))
                return juce::String (number);
        }
    }
    if (CFGetTypeID (value) == CFBooleanGetTypeID())
        return CFBooleanGetValue (static_cast<CFBooleanRef> (value)) ? "true" : "false";
    return {};
}

MacAudioMetadata readMacAudioMetadata (const juce::File& file,
                                       bool includeArtwork)
{
    MacAudioMetadata result;
    const auto fullPath = file.getFullPathName();
    const auto* path = fullPath.toRawUTF8();
    auto* url = CFURLCreateFromFileSystemRepresentation (
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*> (path),
        static_cast<CFIndex> (std::strlen (path)), false);
    if (url == nullptr)
        return result;

    AudioFileID audioFile = nullptr;
    const auto openStatus = AudioFileOpenURL (url, kAudioFileReadPermission,
                                              0, &audioFile);
    CFRelease (url);
    if (openStatus != noErr || audioFile == nullptr)
        return result;

    UInt32 infoSize = sizeof (CFDictionaryRef);
    CFDictionaryRef info = nullptr;
    if (AudioFileGetProperty (audioFile, kAudioFilePropertyInfoDictionary,
                              &infoSize, &info) == noErr
        && info != nullptr)
    {
        const auto count = CFDictionaryGetCount (info);
        constexpr CFIndex maximumMetadataEntries = 512;
        if (count > 0 && count <= maximumMetadataEntries)
        {
            std::vector<const void*> keys (static_cast<size_t> (count));
            std::vector<const void*> values (static_cast<size_t> (count));
            CFDictionaryGetKeysAndValues (info, keys.data(), values.data());
            for (CFIndex index = 0; index < count; ++index)
            {
                if (keys[static_cast<size_t> (index)] == nullptr
                    || CFGetTypeID (keys[static_cast<size_t> (index)])
                           != CFStringGetTypeID())
                    continue;

                const auto key = stringFromCFString (static_cast<CFStringRef> (
                    keys[static_cast<size_t> (index)]));
                const auto* rawValue = values[static_cast<size_t> (index)];
                auto value = stringFromCFValue (rawValue);
                if (normaliseKey (key) == "artist" && rawValue != nullptr
                    && CFGetTypeID (rawValue) == CFArrayGetTypeID())
                {
                    const auto array = static_cast<CFArrayRef> (rawValue);
                    for (CFIndex member = 0; member < juce::jmin (CFArrayGetCount (array), CFIndex { 128 }); ++member)
                    {
                        const auto name = sanitiseMetadataText (stringFromCFValue (CFArrayGetValueAtIndex (array, member)));
                        if (name.isNotEmpty()) result.artists.addIfNotAlreadyThere (name);
                    }
                    value = result.artists.joinIntoString (" / ");
                }
                if (key.isNotEmpty() && value.isNotEmpty())
                    result.values.set (key, value);
            }
        }
        CFRelease (info);
    }

    SInt32 sourceBitDepth = 0;
    UInt32 bitDepthSize = sizeof (sourceBitDepth);
    if (AudioFileGetProperty (audioFile, kAudioFilePropertySourceBitDepth,
                              &bitDepthSize, &sourceBitDepth) == noErr)
        result.sourceBitDepth = juce::jlimit (0, 256,
                                             std::abs (static_cast<int> (sourceBitDepth)));

    AudioFileClose (audioFile);
    if (includeArtwork)
        result.artwork = readMacEmbeddedArtwork (file);
    return result;
}
#endif
} // namespace

TrackMetadataReader::TrackMetadataReader()
{
    formatManager.registerBasicFormats();
}

TrackMetadataResult TrackMetadataReader::read (const juce::File& file,
                                               bool includeArtwork) const
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

    const auto before = trackFileStamp (file);
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
    result.track.bitDepth = static_cast<int> (reader->bitsPerSample);
    result.track.formatName = reader->getFormatName().trim();

    auto metadata = reader->metadataValues;
#if JUCE_MAC
    const auto macMetadata = readMacAudioMetadata (file, includeArtwork);
    const auto macKeys = macMetadata.values.getAllKeys();
    const auto macValues = macMetadata.values.getAllValues();
    for (int index = 0; index < macKeys.size(); ++index)
        metadata.set (macKeys[index], macValues[index]);
    if (macMetadata.sourceBitDepth > 0)
        result.track.bitDepth = macMetadata.sourceBitDepth;
#endif

    // Some decoders omit tags or interpret UTF-8 comments as local text. Prefer
    // validated container tags, while retaining macOS native metadata precedence.
    applyMetadata (result.track, metadata);
#if JUCE_MAC
    const auto preferTaggedMetadata = macMetadata.values.size() == 0;
#else
    constexpr auto preferTaggedMetadata = true;
#endif
    auto fallbackTrack = result.track;
    fallbackTrack.title.clear();
    auto tags = readTrackTags (file);
    const auto& supplementalTags = tags.metadata;
    applyMetadata (fallbackTrack, supplementalTags);
    auto gainTags = metadata;
    gainTags.addArray (supplementalTags);
    result.track.loudness = readReplayGainTags (gainTags);
    if ((preferTaggedMetadata || valueForAliases (metadata, { "title", "id3title", "songname", "inam", "kAFInfoDictionary_Title" }).isEmpty())
        && fallbackTrack.title.isNotEmpty()) result.track.title = fallbackTrack.title;
    if (preferTaggedMetadata || result.track.album.isEmpty()) result.track.album = fallbackTrack.album;
    if (preferTaggedMetadata || result.track.albumArtist.isEmpty()) result.track.albumArtist = fallbackTrack.albumArtist;
    if (preferTaggedMetadata || result.track.genre.isEmpty()) result.track.genre = fallbackTrack.genre;
    if ((preferTaggedMetadata || result.track.year == 0) && fallbackTrack.year > 0) result.track.year = fallbackTrack.year;
    if ((preferTaggedMetadata || result.track.trackNumber == 0) && fallbackTrack.trackNumber > 0) result.track.trackNumber = fallbackTrack.trackNumber;
    if ((preferTaggedMetadata || result.track.discNumber == 0) && fallbackTrack.discNumber > 0) result.track.discNumber = fallbackTrack.discNumber;
    auto artists = std::move (tags.artists);
#if JUCE_MAC
    if (artists.isEmpty()) artists = macMetadata.artists;
#endif
    if (! artists.isEmpty())
    {
        result.track.artists = std::move (artists);
        const auto displayContainsEveryArtist = std::all_of (
            result.track.artists.begin(), result.track.artists.end(), [&] (const auto& name)
                { return result.track.artist.contains (name); });
        if (! displayContainsEveryArtist)
            result.track.artist = result.track.artists.joinIntoString (" / ");
    }
    if (includeArtwork)
    {
#if JUCE_MAC
        result.artworkData = macMetadata.artwork;
#endif
        if (result.artworkData.isEmpty())
            result.artworkData = ArtworkExtractor::find (file, metadata);
    }
    const auto after = trackFileStamp (file);
    if (before.identity.isEmpty() || before != after)
    {
        result.track = TrackItem { file, file.getFileNameWithoutExtension(), 0.0 };
        result.track.available = false;
        result.artworkData = {};
        result.error = "Audio file changed while its metadata was being read.";
        return result;
    }
    result.track.fileSizeBytes = after.size;
    result.track.fileModifiedAtMs = after.modifiedAtMs;
    result.track.fileIdentity = after.identity;
    result.track.available = true;
    result.ok = true;
    return result;
}

void TrackMetadataReader::applyMetadata (TrackItem& track,
                                         const juce::StringPairArray& metadata)
{
    const auto setIfPresent = [&metadata] (juce::String& destination,
                                           std::initializer_list<const char*> aliases)
    {
        if (const auto value = valueForAliases (metadata, aliases); value.isNotEmpty())
            destination = value;
    };

    setIfPresent (track.title, { "title", "id3title", "songname", "inam",
                                "kAFInfoDictionary_Title" });
    setIfPresent (track.artist, { "artist", "id3artist", "author", "auth", "iart",
                                 "kAFInfoDictionary_Artist" });
    track.artists = track.artist.isNotEmpty() ? juce::StringArray { track.artist } : juce::StringArray {};
    setIfPresent (track.album, { "album", "id3album", "iprd",
                                "kAFInfoDictionary_Album" });
    setIfPresent (track.albumArtist, { "albumartist", "id3albumartist",
                                      "kAFInfoDictionary_AlbumArtist" });
    setIfPresent (track.genre, { "genre", "id3genre", "genr", "ignr",
                                "kAFInfoDictionary_Genre" });

    track.year = positiveLeadingInteger (valueForAliases (
        metadata, { "year", "date", "id3year", "id3date", "icrd",
                    "kAFInfoDictionary_Year" }));
    track.trackNumber = positiveLeadingInteger (valueForAliases (
        metadata, { "track", "tracknumber", "id3track", "id3tracknumber",
                    "iprt", "trck", "kAFInfoDictionary_TrackNumber" }));
    track.discNumber = positiveLeadingInteger (valueForAliases (
        metadata, { "disc", "discnumber", "disk", "disknumber",
                    "id3discnumber", "kAFInfoDictionary_DiscNumber" }));
}
