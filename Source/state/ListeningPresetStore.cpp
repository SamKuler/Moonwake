#include "ListeningPresetStore.h"
#include "EffectChainSerialization.h"

#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <set>

namespace
{
using Tree = juce::ValueTree;
constexpr int schemaVersion = 1;
constexpr int maximumNodes = 100000;

bool fail (juce::String& error, const juce::String& message)
{
    error = message;
    return false;
}

bool fields (const Tree& tree, std::initializer_list<const char*> required,
              std::initializer_list<const char*> optional = {})
{
    for (const auto* name : required)
        if (! tree.hasProperty (name)) return false;
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto name = tree.getPropertyName (i).toString();
        const auto matches = [&] (const char* field) { return name == field; };
        if (std::none_of (required.begin(), required.end(), matches)
            && std::none_of (optional.begin(), optional.end(), matches)) return false;
    }
    return true;
}

template <typename Integer>
bool integer (const juce::var& input, Integer& value)
{
    const auto text = input.toString().toStdString();
    const auto result = std::from_chars (text.data(), text.data() + text.size(), value);
    return result.ec == std::errc {} && result.ptr == text.data() + text.size();
}

bool number (const juce::var& input, double& value)
{
    const auto text = input.toString().toStdString();
    if (text.empty()) return false;
    char* end = nullptr;
    value = std::strtod (text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite (value);
}

bool boolean (const juce::var& input, bool& value)
{
    const auto text = input.toString();
    if (text != "0" && text != "1" && text != "false" && text != "true") return false;
    value = text == "1" || text == "true";
    return true;
}

bool singletonChildren (const Tree& tree, std::initializer_list<const char*> required,
                         std::initializer_list<const char*> optional = {})
{
    std::set<std::string> seen;
    for (const auto child : tree)
    {
        const auto type = child.getType().toString();
        const auto matches = [&] (const char* name) { return type == name; };
        if ((! std::any_of (required.begin(), required.end(), matches)
             && ! std::any_of (optional.begin(), optional.end(), matches))
            || ! seen.insert (type.toStdString()).second) return false;
    }
    for (const auto* type : required)
        if (! seen.contains (type)) return false;
    return true;
}

// Bound nesting before XmlDocument enters its recursive parser. Presets have no DTD or mixed text.
bool boundedXml (const juce::String& text)
{
    const auto bytes = text.toStdString();
    size_t cursor = 0;
    int depth = 0, nodes = 0;
    while (cursor < bytes.size())
    {
        if (bytes[cursor] != '<')
        {
            if (! std::isspace (static_cast<unsigned char> (bytes[cursor]))) return false;
            ++cursor;
            continue;
        }
        if (bytes.compare (cursor, 4, "<!--") == 0)
        {
            const auto end = bytes.find ("-->", cursor + 4);
            if (end == std::string::npos) return false;
            cursor = end + 3;
            continue;
        }
        if (cursor + 1 >= bytes.size() || bytes[cursor + 1] == '!') return false;
        char quote = 0;
        size_t end = cursor + 1;
        for (; end < bytes.size(); ++end)
        {
            const char c = bytes[end];
            if (quote != 0) { if (c == quote) quote = 0; }
            else if (c == '\'' || c == '"') quote = c;
            else if (c == '>') break;
        }
        if (end == bytes.size()) return false;
        if (bytes[cursor + 1] == '?')
        {
            if (bytes.compare (cursor, 5, "<?xml") != 0 || bytes[end - 1] != '?') return false;
        }
        else if (bytes[cursor + 1] == '/') { if (--depth < 0) return false; }
        else
        {
            if (++nodes > maximumNodes) return false;
            if (++depth > 16) return false;
            if (bytes[end - 1] == '/') --depth;
        }
        cursor = end + 1;
    }
    return depth == 0;
}

bool readTree (const juce::File& file, Tree& tree, juce::String& error)
{
    auto stream = file.createInputStream();
    if (stream == nullptr) return fail (error, "Could not read preset file: " + file.getFullPathName());
    juce::MemoryBlock bytes;
    stream->readIntoMemoryBlock (bytes, static_cast<std::ptrdiff_t> (ListeningPresetStore::maximumFileBytes + 1));
    if (stream->getStatus().failed()) return fail (error, "Could not finish reading preset file: " + file.getFullPathName());
    if (bytes.getSize() == 0 || bytes.getSize() > static_cast<size_t> (ListeningPresetStore::maximumFileBytes))
        return fail (error, "Preset file is empty or exceeds the 64 MiB limit: " + file.getFullPathName());
    const auto text = juce::String::fromUTF8 (static_cast<const char*> (bytes.getData()), static_cast<int> (bytes.getSize()));
    if (! boundedXml (text)) return fail (error, "Preset XML has unsupported content or excessive nesting.");
    auto xml = juce::XmlDocument::parse (text);
    if (xml == nullptr) return fail (error, "Preset file is not valid XML: " + file.getFullPathName());
    tree = Tree::fromXml (*xml);
    return true;
}

bool validEncodedState (const juce::String& encoded)
{
    const auto separator = encoded.indexOfChar ('.');
    if (separator < 1) return false;
    size_t bytes = 0;
    if (! integer (juce::var (encoded.substring (0, separator)), bytes) || bytes > 16 * 1024 * 1024)
        return false;
    const auto data = encoded.substring (separator + 1);
    if (static_cast<size_t> (data.length()) != (bytes * 8 + 5) / 6
        || ! data.containsOnly (".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+")) return false;
    juce::MemoryBlock decoded;
    return decoded.fromBase64Encoding (encoded) && decoded.toBase64Encoding() == encoded;
}

bool readChain (const Tree& tree, EffectChainState& chain, juce::String& error)
{
    if (! tree.hasType ("EffectChain") || ! fields (tree, {}) || tree.getNumChildren() > 128)
        return fail (error, "Invalid effect chain or too many effect slots.");
    std::set<int> ids;
    for (const auto slot : tree)
    {
        int id = 0;
        bool bypassed = false, available = true;
        if (! slot.hasType ("Slot")
            || ! fields (slot, { "id", "pluginName", "pluginIdentifier", "pluginFormat", "bypassed", "available", "errorMessage" }, { "serializedState" })
            || ! singletonChildren (slot, {}, { "Scope" })
            || ! integer (slot["id"], id) || id <= 0 || id == std::numeric_limits<int>::max()
            || ! ids.insert (id).second || ! boolean (slot["bypassed"], bypassed)
            || ! boolean (slot["available"], available))
            return fail (error, "Effect slot has invalid fields or a duplicate ID.");
        const auto scope = slot.getChildWithName ("Scope");
        if (scope.isValid() && (! fields (scope, {}) || scope.getNumChildren() > 2))
            return fail (error, "Effect scope is invalid.");
        for (const auto speaker : scope)
        {
            int type = 0;
            if (! speaker.hasType ("Channel") || ! fields (speaker, { "type" })
                || speaker.getNumChildren() != 0 || ! integer (speaker["type"], type))
                return fail (error, "Effect scope speaker is invalid.");
        }
        if (slot.hasProperty ("serializedState") && ! validEncodedState (slot["serializedState"].toString()))
            return fail (error, "Effect state has invalid encoding or exceeds the 16 MiB limit.");
    }
    auto candidate = EffectChainSerialization::fromValueTree (tree);
    const auto valid = validateEffectChainSnapshot (candidate);
    if (valid.failed()) return fail (error, valid.getErrorMessage());
    chain = std::move (candidate);
    return true;
}

Tree outputToTree (const ListeningOutputConfiguration& output)
{
    Tree tree ("Output");
    tree.setProperty ("deviceIdentifier", output.deviceIdentifier, nullptr);
    tree.setProperty ("deviceName", output.deviceName, nullptr);
    tree.setProperty ("layout", toString (output.layout), nullptr);
    tree.setProperty ("sampleRate", output.sampleRate, nullptr);
    tree.setProperty ("bufferSize", output.bufferSize, nullptr);
    Tree routing ("Routing"), mixes ("ChannelMix"), calibration ("Calibration");
    for (const auto port : output.routing)
    {
        Tree channel ("Channel"); channel.setProperty ("port", port, nullptr);
        routing.appendChild (channel, nullptr);
    }
    for (const auto& mix : output.channelMix)
    {
        Tree channel ("Channel");
        channel.setProperty ("type", static_cast<int> (mix.channel), nullptr);
        channel.setProperty ("gain", mix.gain, nullptr);
        channel.setProperty ("muted", mix.muted, nullptr);
        channel.setProperty ("solo", mix.solo, nullptr);
        mixes.appendChild (channel, nullptr);
    }
    for (const auto& value : output.calibration)
    {
        Tree channel ("Channel");
        channel.setProperty ("type", static_cast<int> (value.channel), nullptr);
        channel.setProperty ("trimDb", value.trimDb, nullptr);
        channel.setProperty ("delayMs", value.delayMs, nullptr);
        channel.setProperty ("inverted", value.inverted, nullptr);
        calibration.appendChild (channel, nullptr);
    }
    tree.appendChild (routing, nullptr);
    tree.appendChild (mixes, nullptr);
    tree.appendChild (calibration, nullptr);
    return tree;
}

bool readOutput (const Tree& tree, ListeningOutputConfiguration& output, juce::String& error)
{
    if (! tree.hasType ("Output") || ! fields (tree, { "deviceIdentifier", "deviceName", "layout", "sampleRate", "bufferSize" })
        || ! singletonChildren (tree, { "Routing", "ChannelMix", "Calibration" }))
        return fail (error, "Output configuration fields are invalid.");
    output.deviceIdentifier = tree["deviceIdentifier"].toString();
    output.deviceName = tree["deviceName"].toString();
    output.layout = outputLayoutFromString (tree["layout"].toString());
    if (toString (output.layout) != tree["layout"].toString()
        || ! number (tree["sampleRate"], output.sampleRate) || ! integer (tree["bufferSize"], output.bufferSize))
        return fail (error, "Output layout, sample rate or buffer size is invalid.");
    for (const auto list : tree)
    {
        if (! fields (list, {}) || list.getNumChildren() > 64)
            return fail (error, "Output channel list is invalid.");
        for (const auto channel : list)
        {
            if (! channel.hasType ("Channel") || channel.getNumChildren() != 0)
                return fail (error, "Output channel entry is invalid.");
            int type = 0;
            if (list.hasType ("Routing"))
            {
                int port = 0;
                if (! fields (channel, { "port" }) || ! integer (channel["port"], port))
                    return fail (error, "Routing port is invalid.");
                output.routing.push_back (port);
                continue;
            }
            if (! integer (channel["type"], type)) return fail (error, "Speaker type is invalid.");
            if (list.hasType ("ChannelMix"))
            {
                OutputChannelMix mix;
                double gain = 0.0;
                if (! fields (channel, { "type", "gain", "muted", "solo" })
                    || ! number (channel["gain"], gain) || ! boolean (channel["muted"], mix.muted)
                    || ! boolean (channel["solo"], mix.solo)) return fail (error, "Channel mix is invalid.");
                mix.channel = static_cast<juce::AudioChannelSet::ChannelType> (type);
                mix.gain = static_cast<float> (gain);
                output.channelMix.push_back (mix);
            }
            else
            {
                OutputChannelCalibration value;
                double trim = 0.0;
                if (! fields (channel, { "type", "trimDb", "delayMs", "inverted" })
                    || ! number (channel["trimDb"], trim) || ! number (channel["delayMs"], value.delayMs)
                    || ! boolean (channel["inverted"], value.inverted)) return fail (error, "Calibration is invalid.");
                value.channel = static_cast<juce::AudioChannelSet::ChannelType> (type);
                value.trimDb = static_cast<float> (trim);
                output.calibration.push_back (value);
            }
        }
    }
    const auto valid = validateListeningOutputConfiguration (output);
    return valid.wasOk() || fail (error, valid.getErrorMessage());
}

template <typename Preset>
Tree presetToTree (const Preset& preset)
{
    Tree tree (std::is_same_v<Preset, ListeningPreset> ? "ListeningPreset" : "EffectChainPreset");
    tree.setProperty ("id", preset.id, nullptr);
    tree.setProperty ("name", preset.name.trim(), nullptr);
    tree.setProperty ("createdAtMs", preset.createdAtMs, nullptr);
    tree.setProperty ("updatedAtMs", preset.updatedAtMs, nullptr);
    tree.appendChild (EffectChainSerialization::toValueTree (preset.chain), nullptr);
    if constexpr (std::is_same_v<Preset, ListeningPreset>) tree.appendChild (outputToTree (preset.output), nullptr);
    return tree;
}

template <typename Preset>
bool readPreset (const Tree& tree, Preset& preset, juce::String& error)
{
    if (! tree.hasType (std::is_same_v<Preset, ListeningPreset> ? "ListeningPreset" : "EffectChainPreset")
        || ! fields (tree, { "id", "name", "createdAtMs", "updatedAtMs" })
        || ! integer (tree["createdAtMs"], preset.createdAtMs) || ! integer (tree["updatedAtMs"], preset.updatedAtMs))
        return fail (error, "Preset identity or timestamps are invalid.");
    preset.id = tree["id"].toString();
    preset.name = tree["name"].toString().trim();
    if constexpr (std::is_same_v<Preset, ListeningPreset>)
    {
        if (! singletonChildren (tree, { "Output", "EffectChain" })
            || ! readOutput (tree.getChildWithName ("Output"), preset.output, error))
            return fail (error, error.isEmpty() ? "Listening preset has invalid sections." : error);
    }
    else if (! singletonChildren (tree, { "EffectChain" })) return fail (error, "Effect preset has invalid sections.");
    if (! readChain (tree.getChildWithName ("EffectChain"), preset.chain, error)) return false;
    const auto valid = validatePreset (preset);
    return valid.wasOk() || fail (error, valid.getErrorMessage());
}

enum class LibraryReadStatus { loaded, invalid, newerVersion };

LibraryReadStatus readLibrary (const juce::File& file, ListeningPresetLibrary& library, juce::String& error)
{
    const auto reject = [&] (const juce::String& message)
    {
        error = message;
        return LibraryReadStatus::invalid;
    };
    Tree tree;
    if (! readTree (file, tree, error)) return LibraryReadStatus::invalid;
    int version = 0;
    if (tree.hasType ("MoonwakePresets") && integer (tree["schemaVersion"], version) && version > schemaVersion)
    {
        error = "Preset library was created by a newer version of Moonwake: " + file.getFullPathName();
        return LibraryReadStatus::newerVersion;
    }
    if (! tree.hasType ("MoonwakePresets") || ! fields (tree, { "schemaVersion" })
        || ! integer (tree["schemaVersion"], version) || version != schemaVersion || tree.getNumChildren() > 1024)
        return reject ("Unsupported preset library format or version.");
    ListeningPresetLibrary candidate;
    std::set<std::string> ids;
    for (const auto child : tree)
    {
        if (! ids.insert (child["id"].toString().toStdString()).second)
            return reject ("The preset library contains duplicate IDs.");
        juce::Result result = juce::Result::ok();
        if (child.hasType ("ListeningPreset"))
        {
            ListeningPreset preset;
            if (! readPreset (child, preset, error)) return LibraryReadStatus::invalid;
            result = candidate.put (std::move (preset));
        }
        else
        {
            EffectChainPreset preset;
            if (! readPreset (child, preset, error)) return LibraryReadStatus::invalid;
            result = candidate.put (std::move (preset));
        }
        if (result.failed()) return reject (result.getErrorMessage());
    }
    library = std::move (candidate);
    return LibraryReadStatus::loaded;
}

bool serializeTree (const Tree& tree, juce::String& content, juce::String& error)
{
    content = tree.createXml()->toString();
    if (content.getNumBytesAsUTF8() > static_cast<size_t> (ListeningPresetStore::maximumFileBytes))
        return fail (error, "Preset data exceeds the 64 MiB file limit.");
    if (! boundedXml (content))
        return fail (error, "Preset data exceeds the supported XML structure limits.");
    return true;
}

bool writeContent (const juce::String& content, const juce::File& file, juce::String& error)
{
    if (! file.getParentDirectory().createDirectory())
        return fail (error, "Could not create preset directory: " + file.getParentDirectory().getFullPathName());
    juce::TemporaryFile temporary (file);
    if (! temporary.getFile().replaceWithText (content)
        || ! temporary.overwriteTargetFileWithTemporary())
        return fail (error, "Could not write preset file: " + file.getFullPathName());
    return true;
}

template <typename Preset>
bool exportOne (const Preset& preset, const juce::File& file, juce::String& error)
{
    error.clear();
    const auto valid = validatePreset (preset);
    if (valid.failed()) return fail (error, valid.getErrorMessage());
    Tree tree ("MoonwakePreset");
    tree.setProperty ("schemaVersion", schemaVersion, nullptr);
    tree.setProperty ("kind", std::is_same_v<Preset, ListeningPreset> ? "listening" : "effect-chain", nullptr);
    tree.appendChild (presetToTree (preset), nullptr);
    juce::String content;
    return serializeTree (tree, content, error) && writeContent (content, file, error);
}
}

juce::File ListeningPresetStore::backupFileFor (const juce::File& file)
{
    return file.getSiblingFile (file.getFileName() + ".bak");
}

bool ListeningPresetStore::writeAtomically (const ListeningPresetLibrary& library, const juce::File& file, juce::String& error)
{
    error.clear();
    Tree tree ("MoonwakePresets");
    tree.setProperty ("schemaVersion", schemaVersion, nullptr);
    std::set<std::string> ids;
    const auto append = [&] (const auto& presets)
    {
        for (const auto& preset : presets)
        {
            const auto valid = validatePreset (preset);
            if (valid.failed()) return fail (error, valid.getErrorMessage());
            if (! ids.insert (preset.id.toStdString()).second) return fail (error, "Preset IDs must be unique across the library.");
            tree.appendChild (presetToTree (preset), nullptr);
        }
        return true;
    };
    if (! append (library.getEffectChainPresets()) || ! append (library.getListeningPresets())) return false;
    juce::String content;
    if (! serializeTree (tree, content, error)) return false;
    if (file.existsAsFile() || backupFileFor (file).existsAsFile())
    {
        ListeningPresetLibrary existing;
        juce::String readError;
        const auto status = readLibrary (file, existing, readError);
        if (status == LibraryReadStatus::newerVersion) return fail (error, readError);
        if (status == LibraryReadStatus::loaded)
        {
            juce::TemporaryFile backup (backupFileFor (file));
            if (! file.copyFileTo (backup.getFile()) || ! backup.overwriteTargetFileWithTemporary())
                return fail (error, "Could not preserve preset backup: " + backupFileFor (file).getFullPathName());
        }
        else if (readLibrary (backupFileFor (file), existing, readError) != LibraryReadStatus::loaded)
            return fail (error, "Existing preset data could not be recovered; files were preserved: " + file.getFullPathName() + ". " + readError);
    }
    return writeContent (content, file, error);
}

bool ListeningPresetStore::readFromFile (const juce::File& file, ListeningPresetLibrary& library, juce::String& error)
{
    error.clear();
    ListeningPresetLibrary candidate;
    const auto status = readLibrary (file, candidate, error);
    if (status == LibraryReadStatus::loaded) { library = std::move (candidate); return true; }
    if (status == LibraryReadStatus::newerVersion) return false;
    const auto originalError = error;
    juce::String backupError;
    if (readLibrary (backupFileFor (file), candidate, backupError) == LibraryReadStatus::loaded)
    {
        library = std::move (candidate);
        error = "Recovered presets from backup: " + backupFileFor (file).getFullPathName() + ". " + originalError;
        return true;
    }
    error = originalError + " Backup recovery failed: " + backupError;
    return false;
}

bool ListeningPresetStore::exportPreset (const EffectChainPreset& preset, const juce::File& file, juce::String& error) { return exportOne (preset, file, error); }
bool ListeningPresetStore::exportPreset (const ListeningPreset& preset, const juce::File& file, juce::String& error) { return exportOne (preset, file, error); }

bool ListeningPresetStore::importPreset (const juce::File& file, ImportedListeningPreset& preset, juce::String& error)
{
    error.clear();
    Tree tree;
    if (! readTree (file, tree, error)) return false;
    int version = 0;
    if (! tree.hasType ("MoonwakePreset") || ! fields (tree, { "schemaVersion", "kind" })
        || ! integer (tree["schemaVersion"], version) || version != schemaVersion || tree.getNumChildren() != 1)
        return fail (error, "Unsupported preset format or version.");
    if (tree["kind"].toString() == "listening")
    {
        ListeningPreset candidate;
        if (! readPreset (tree.getChild (0), candidate, error)) return false;
        preset = std::move (candidate);
        return true;
    }
    if (tree["kind"].toString() == "effect-chain")
    {
        EffectChainPreset candidate;
        if (! readPreset (tree.getChild (0), candidate, error)) return false;
        preset = std::move (candidate);
        return true;
    }
    return fail (error, "Preset kind must be listening or effect-chain.");
}
