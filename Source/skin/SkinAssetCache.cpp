#include "SkinAssetCache.h"

#include "SkinRenderDefinition.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string_view>

namespace
{
using ElementSet = std::set<juce::String>;

std::mutex& packageFontMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::set<juce::String>& packageFontFamilies()
{
    // JUCE keeps a data-backed typeface globally registered while any Font retains
    // its Typeface::Ptr, which can outlive the originating skin cache. Keep this
    // deny-list for the process lifetime so a later skin never resolves that face
    // through an unscoped family-name fallback.
    static std::set<juce::String> families;
    return families;
}

juce::String normalisedFontFamily (juce::String family)
{
    return family.trim().toLowerCase();
}

const ElementSet& allowedElements()
{
    static const ElementSet values {
        "svg", "g", "path", "rect", "circle", "ellipse", "line", "polyline",
        "polygon", "defs", "linearGradient", "radialGradient", "stop", "clipPath"
    };
    return values;
}

const ElementSet& globalAttributes()
{
    static const ElementSet values {
        "fill", "stroke", "stroke-width", "stroke-linecap", "stroke-linejoin",
        "opacity", "transform", "id", "clip-path"
    };
    return values;
}

bool isAllowedAttributeForElement (const juce::String& tag, const juce::String& name)
{
    if (globalAttributes().contains (name)) return true;
    const auto isOneOf = [&] (std::initializer_list<const char*> names)
    {
        return std::any_of (names.begin(), names.end(), [&] (const char* value)
        { return name == value; });
    };
    if (tag == "svg") return isOneOf ({ "x", "y", "width", "height", "viewBox" });
    if (tag == "path") return name == "d";
    if (tag == "rect") return isOneOf ({ "x", "y", "width", "height", "rx", "ry" });
    if (tag == "circle") return isOneOf ({ "cx", "cy", "r" });
    if (tag == "ellipse") return isOneOf ({ "cx", "cy", "rx", "ry" });
    if (tag == "line") return isOneOf ({ "x1", "y1", "x2", "y2" });
    if (tag == "polyline" || tag == "polygon") return name == "points";
    if (tag == "linearGradient")
        return isOneOf ({ "x1", "y1", "x2", "y2", "gradientUnits",
                          "gradientTransform" });
    if (tag == "radialGradient")
        return isOneOf ({ "cx", "cy", "r", "fx", "fy", "gradientUnits",
                          "gradientTransform" });
    if (tag == "stop")
        return isOneOf ({ "offset", "stop-color", "stop-opacity" });
    return false;
}

void addAssetError (SkinValidationReport& report, juce::String code,
                    const juce::String& file, juce::String message)
{
    report.addError (std::move (code), file, {}, std::move (message));
}

bool containsForbiddenXmlDeclaration (const juce::String& text)
{
    const auto lower = text.toLowerCase();
    return lower.contains ("<!doctype") || lower.contains ("<!entity");
}

bool validLocalUrlReferences (const juce::String& value,
                              std::set<juce::String>* references = nullptr)
{
    auto remaining = value;
    for (;;)
    {
        const auto start = remaining.indexOfIgnoreCase ("url(");
        if (start < 0) return true;
        remaining = remaining.substring (start + 4);
        const auto close = remaining.indexOfChar (')');
        if (close < 0) return false;
        auto reference = remaining.substring (0, close).trim();
        if ((reference.startsWithChar ('\'') && reference.endsWithChar ('\''))
            || (reference.startsWithChar ('"') && reference.endsWithChar ('"')))
            reference = reference.substring (1, reference.length() - 1).trim();
        if (! reference.startsWithChar ('#') || reference.length() < 2
            || reference.containsAnyOf ("/:\\"))
            return false;
        if (references != nullptr) references->insert (reference.substring (1));
        remaining = remaining.substring (close + 1);
    }
}

int countSvgPoints (const char* begin, const char* end, int maximumCommands,
                    bool closesPath, bool& budgetExceeded)
{
    int numberCount = 0;
    auto* cursor = begin;
    bool previousWasNumber = false;
    while (cursor < end)
    {
        int commaCount = 0;
        while (cursor < end && (*cursor == ' ' || *cursor == '\t'
                                || *cursor == '\r' || *cursor == '\n'
                                || *cursor == ','))
        {
            if (*cursor++ == ',') ++commaCount;
        }
        if (cursor == end)
        {
            if (commaCount != 0 || numberCount < 2 || numberCount % 2 != 0)
                return -1;
            const auto commands = numberCount / 2 + (closesPath ? 1 : 0);
            if (commands > maximumCommands)
            {
                budgetExceeded = true;
                return -1;
            }
            return commands;
        }
        if (commaCount > 1 || (commaCount == 1 && ! previousWasNumber)) return -1;

        const auto* start = cursor;
        if (*cursor == '+' || *cursor == '-') ++cursor;
        int digits = 0;
        while (cursor < end && std::isdigit (static_cast<unsigned char> (*cursor)))
        {
            ++cursor;
            ++digits;
        }
        if (cursor < end && *cursor == '.')
        {
            ++cursor;
            while (cursor < end && std::isdigit (static_cast<unsigned char> (*cursor)))
            {
                ++cursor;
                ++digits;
            }
        }
        if (digits == 0) return -1;
        if (cursor < end && (*cursor == 'e' || *cursor == 'E'))
        {
            ++cursor;
            if (cursor < end && (*cursor == '+' || *cursor == '-')) ++cursor;
            int exponentDigits = 0;
            while (cursor < end && std::isdigit (static_cast<unsigned char> (*cursor)))
            {
                ++cursor;
                ++exponentDigits;
            }
            if (exponentDigits == 0) return -1;
        }
        const auto length = static_cast<size_t> (cursor - start);
        if (length == 0 || length > 64) return -1;
        char numberText[65] {};
        std::copy (start, cursor, numberText);
        char* parsedEnd = nullptr;
        const auto value = std::strtod (numberText, &parsedEnd);
        if (parsedEnd != numberText + length || ! std::isfinite (value)) return -1;
        previousWasNumber = true;
        ++numberCount;
        if (numberCount / 2 > maximumCommands)
        {
            budgetExceeded = true;
            return -1;
        }
    }
    if (numberCount < 2 || numberCount % 2 != 0) return -1;
    const auto commands = numberCount / 2 + (closesPath ? 1 : 0);
    if (commands > maximumCommands)
    {
        budgetExceeded = true;
        return -1;
    }
    return commands;
}

struct SvgPreflightStats
{
    int elements = 0;
    int pathCommands = 0;
};

std::optional<SvgPreflightStats> preflightSvgText (
    const juce::String& text, int maximumPathCommands,
    SkinValidationReport& report, const juce::String& path)
{
    constexpr auto maximumBoundedAttributeBytes = 4096;
    const auto* cursor = text.toRawUTF8();
    const auto* end = cursor + text.getNumBytesAsUTF8();
    SvgPreflightStats stats;
    const auto fail = [&] (juce::String code, juce::String message)
        -> std::optional<SvgPreflightStats>
    {
        addAssetError (report, std::move (code), path, std::move (message));
        return std::nullopt;
    };
    const auto isNameCharacter = [] (char value)
    {
        return std::isalnum (static_cast<unsigned char> (value))
            || value == '_' || value == '-' || value == ':';
    };
    const auto skipWhitespace = [&] (const char*& position)
    {
        while (position < end && std::isspace (static_cast<unsigned char> (*position)))
            ++position;
    };

    bool sawRoot = false;
    while (cursor < end)
    {
        if (*cursor != '<')
        {
            ++cursor;
            continue;
        }
        if (end - cursor >= 4 && std::equal (cursor, cursor + 4, "<!--"))
        {
            constexpr std::string_view marker { "-->" };
            const auto close = std::search (
                cursor + 4, end, marker.begin(), marker.end());
            if (close == end) return fail ("ASSET006", "SVG comment is not terminated.");
            cursor = close + 3;
            continue;
        }
        if (end - cursor >= 2 && cursor[1] == '?')
        {
            constexpr std::string_view marker { "?>" };
            const auto close = std::search (
                cursor + 2, end, marker.begin(), marker.end());
            if (close == end || sawRoot
                || end - cursor < 6
                || ! std::equal (cursor + 2, cursor + 5, "xml")
                || (! std::isspace (static_cast<unsigned char> (cursor[5]))
                    && cursor[5] != '?'))
                return fail ("ASSET006", "SVG processing instruction is not allowed.");
            cursor = close + 2;
            continue;
        }
        if (end - cursor >= 2 && cursor[1] == '!')
            return fail ("ASSET006", "SVG declarations and CDATA are not allowed.");
        if (end - cursor >= 2 && cursor[1] == '/')
        {
            const auto close = std::find (cursor + 2, end, '>');
            if (close == end) return fail ("ASSET006", "SVG closing tag is not terminated.");
            cursor = close + 1;
            continue;
        }

        ++cursor;
        const auto* tagStart = cursor;
        while (cursor < end && isNameCharacter (*cursor)) ++cursor;
        if (cursor == tagStart) return fail ("ASSET006", "SVG opening tag is invalid.");
        const auto tag = juce::String::fromUTF8 (
            tagStart, static_cast<int> (cursor - tagStart));
        sawRoot = true;
        if (++stats.elements > SkinAssetCache::maximumSvgElements)
            return fail ("ASSET010", "SVG contains more than 10000 elements before parsing.");

        for (;;)
        {
            skipWhitespace (cursor);
            if (cursor >= end) return fail ("ASSET006", "SVG opening tag is not terminated.");
            if (*cursor == '>')
            {
                ++cursor;
                break;
            }
            if (*cursor == '/' && cursor + 1 < end && cursor[1] == '>')
            {
                cursor += 2;
                break;
            }
            const auto* nameStart = cursor;
            while (cursor < end && isNameCharacter (*cursor)) ++cursor;
            if (cursor == nameStart) return fail ("ASSET006", "SVG attribute name is invalid.");
            const auto attribute = juce::String::fromUTF8 (
                nameStart, static_cast<int> (cursor - nameStart));
            skipWhitespace (cursor);
            if (cursor >= end || *cursor++ != '=')
                return fail ("ASSET006", "SVG attribute must have a quoted value.");
            skipWhitespace (cursor);
            if (cursor >= end || (*cursor != '\'' && *cursor != '"'))
                return fail ("ASSET006", "SVG attribute value must be quoted.");
            const auto quote = *cursor++;
            const auto* valueStart = cursor;
            while (cursor < end && *cursor != quote) ++cursor;
            if (cursor == end) return fail ("ASSET006", "SVG attribute value is not terminated.");
            const auto* valueEnd = cursor++;

            const auto isPathData = tag == "path" && attribute == "d";
            const auto isPointData = (tag == "polyline" || tag == "polygon")
                                  && attribute == "points";
            if (! isPathData && ! isPointData
                && valueEnd - valueStart > maximumBoundedAttributeBytes)
                return fail ("ASSET010",
                             "SVG non-geometry attribute exceeds the 4096-byte limit before parsing.");

            if (isPathData)
            {
                SkinSvgPathCompileError error = SkinSvgPathCompileError::none;
                const auto value = juce::String::fromUTF8 (
                    valueStart, static_cast<int> (valueEnd - valueStart));
                const auto compiled = skinCompileSvgPathData (
                    value, maximumPathCommands - stats.pathCommands, &error);
                if (! compiled)
                    return fail (error == SkinSvgPathCompileError::commandBudgetExceeded
                                    ? "ASSET010" : "ASSET006",
                                 error == SkinSvgPathCompileError::commandBudgetExceeded
                                    ? "SVG package path-command budget is exceeded before parsing."
                                    : "SVG path data is outside the frozen grammar.");
                stats.pathCommands += compiled->commandCount;
            }
            else if (isPointData)
            {
                bool budgetExceeded = false;
                const auto commands = countSvgPoints (
                    valueStart, valueEnd,
                    maximumPathCommands - stats.pathCommands,
                    tag == "polygon", budgetExceeded);
                if (commands < 0)
                    return fail (budgetExceeded ? "ASSET010" : "ASSET006",
                                 budgetExceeded
                                    ? "SVG package geometry budget is exceeded before parsing."
                                    : "SVG points data is outside the frozen grammar.");
                stats.pathCommands += commands;
            }
        }
    }
    return stats.elements > 0 ? std::optional<SvgPreflightStats> (stats)
                              : fail ("ASSET006", "SVG contains no root element.");
}

bool validateSvgElement (const juce::XmlElement& element, int depth,
                         int& elements, int& pathCommands,
                         int maximumPathCommands,
                         std::set<juce::String>& ids,
                         std::set<juce::String>& references,
                         SkinValidationReport& report, const juce::String& path)
{
    if (depth > 32)
    {
        addAssetError (report, "ASSET010", path, "SVG nesting exceeds 32 levels.");
        return false;
    }
    if (! allowedElements().contains (element.getTagName()))
    {
        addAssetError (report, "ASSET007", path,
                       "SVG element is not allowed: " + element.getTagName() + ".");
        return false;
    }
    if (++elements > SkinAssetCache::maximumSvgElements)
    {
        addAssetError (report, "ASSET010", path,
                       "SVG contains more than 10000 elements.");
        return false;
    }

    for (int i = 0; i < element.getNumAttributes(); ++i)
    {
        const auto name = element.getAttributeName (i);
        const auto value = element.getAttributeValue (i);
        if (! isAllowedAttributeForElement (element.getTagName(), name)
            || name.startsWithIgnoreCase ("on"))
        {
            addAssetError (report, "ASSET008", path,
                           "SVG attribute is not allowed: " + name + ".");
            return false;
        }
        if (value.containsIgnoreCase ("url(")
            && ! validLocalUrlReferences (value, &references))
        {
            addAssetError (report, "ASSET009", path,
                           "SVG url() references must use a same-file #id.");
            return false;
        }
        if (name == "id" && (value.isEmpty() || ! ids.insert (value).second))
        {
            addAssetError (report, "ASSET009", path,
                           "SVG ids must be non-empty and unique within the file.");
            return false;
        }
        if (name == "d")
        {
            SkinSvgPathCompileError pathError = SkinSvgPathCompileError::none;
            const auto remainingCommands = maximumPathCommands - pathCommands;
            const auto compiled = skinCompileSvgPathData (
                value, remainingCommands, &pathError);
            if (! compiled)
            {
                if (pathError == SkinSvgPathCompileError::commandBudgetExceeded)
                    addAssetError (report, "ASSET010", path,
                                   "SVG package path-command budget is exceeded.");
                else
                    addAssetError (report, "ASSET006", path,
                                   "SVG path data is outside the frozen grammar.");
                return false;
            }
            pathCommands += compiled->commandCount;
        }
        else if (name == "points")
        {
            const auto* begin = value.toRawUTF8();
            const auto* end = begin + value.getNumBytesAsUTF8();
            bool budgetExceeded = false;
            const auto commands = countSvgPoints (
                begin, end, maximumPathCommands - pathCommands,
                element.getTagName() == "polygon", budgetExceeded);
            if (commands < 0)
            {
                addAssetError (report, budgetExceeded ? "ASSET010" : "ASSET006", path,
                               budgetExceeded ? "SVG package geometry budget is exceeded."
                                              : "SVG points data is outside the frozen grammar.");
                return false;
            }
            pathCommands += commands;
        }
    }

    for (auto* child = element.getFirstChildElement(); child != nullptr;
         child = child->getNextElement())
        if (! validateSvgElement (*child, depth + 1, elements, pathCommands,
                                  maximumPathCommands,
                                  ids, references, report, path))
            return false;
    return true;
}

bool isBitmapExtension (const juce::String& extension)
{
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg"
        || extension == ".gif";
}

bool isFontExtension (const juce::String& extension)
{
    return extension == ".ttf" || extension == ".otf";
}

uint16_t readBigEndian16 (const uint8_t* bytes)
{
    return static_cast<uint16_t> ((static_cast<uint16_t> (bytes[0]) << 8) | bytes[1]);
}

uint32_t readBigEndian32 (const uint8_t* bytes)
{
    return (static_cast<uint32_t> (bytes[0]) << 24)
         | (static_cast<uint32_t> (bytes[1]) << 16)
         | (static_cast<uint32_t> (bytes[2]) << 8)
         | static_cast<uint32_t> (bytes[3]);
}

std::optional<std::pair<int, int>> probeBitmapDimensions (
    const juce::MemoryBlock& block, const juce::String& extension)
{
    const auto* bytes = static_cast<const uint8_t*> (block.getData());
    const auto size = block.getSize();
    if (extension == ".png" && size >= 24
        && std::memcmp (bytes, "\x89PNG\r\n\x1a\n", 8) == 0)
        return std::pair (static_cast<int> (readBigEndian32 (bytes + 16)),
                          static_cast<int> (readBigEndian32 (bytes + 20)));
    if (extension == ".gif" && size >= 10
        && (std::memcmp (bytes, "GIF87a", 6) == 0
            || std::memcmp (bytes, "GIF89a", 6) == 0))
        return std::pair (static_cast<int> (bytes[6] | (bytes[7] << 8)),
                          static_cast<int> (bytes[8] | (bytes[9] << 8)));
    if ((extension == ".jpg" || extension == ".jpeg")
        && size >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8)
    {
        size_t cursor = 2;
        while (cursor + 3 < size)
        {
            while (cursor < size && bytes[cursor] == 0xff) ++cursor;
            if (cursor >= size) break;
            const auto marker = bytes[cursor++];
            if (marker == 0xd8 || marker == 0x01) continue;
            if (marker == 0xd9 || marker == 0xda) break;
            if (cursor + 2 > size) break;
            const auto length = static_cast<size_t> (readBigEndian16 (bytes + cursor));
            if (length < 2 || cursor + length > size) break;
            const auto isStartOfFrame =
                (marker >= 0xc0 && marker <= 0xc3)
                || (marker >= 0xc5 && marker <= 0xc7)
                || (marker >= 0xc9 && marker <= 0xcb)
                || (marker >= 0xcd && marker <= 0xcf);
            if (isStartOfFrame && length >= 7)
                return std::pair (
                    static_cast<int> (readBigEndian16 (bytes + cursor + 5)),
                    static_cast<int> (readBigEndian16 (bytes + cursor + 3)));
            cursor += length;
        }
    }
    return std::nullopt;
}

juce::String extensionOf (const juce::String& path)
{
    const auto dot = path.lastIndexOfChar ('.');
    return dot >= 0 ? path.substring (dot).toLowerCase() : juce::String();
}
} // namespace

std::optional<std::shared_ptr<SkinAssetCache>> SkinAssetCache::create (
    const SkinPackage& package, juce::String skinId, juce::String skinVersion,
    const std::set<juce::String>& referencedAssets,
    const std::vector<SkinFontValue>& referencedFonts,
    SkinValidationReport& report)
{
    auto cache = std::shared_ptr<SkinAssetCache> (new SkinAssetCache());
    cache->cacheNamespace = std::move (skinId) + "@" + std::move (skinVersion)
                          + "#" + package.sha256();

    for (const auto& path : referencedAssets)
    {
        if (! SkinPackage::isSafeRelativePath (path) || ! package.contains (path))
            addAssetError (report, "ASSET001", path, "Referenced asset does not exist.");
    }
    for (const auto& font : referencedFonts)
        if (font.source.isNotEmpty()
            && (! SkinPackage::isSafeRelativePath (font.source)
                || ! package.contains (font.source)))
            addAssetError (report, "ASSET001", font.source,
                           "Referenced font asset does not exist.");
    if (report.hasErrors()) return std::nullopt;

    std::set<juce::String> uniqueAssetFiles;
    for (const auto& path : package.listFiles())
        if (path.startsWith ("assets/")) uniqueAssetFiles.insert (path);
    uniqueAssetFiles.insert (referencedAssets.begin(), referencedAssets.end());
    for (const auto& font : referencedFonts)
        if (font.source.isNotEmpty()) uniqueAssetFiles.insert (font.source);
    cache->stats.assetFiles = static_cast<int> (uniqueAssetFiles.size());
    if (cache->stats.assetFiles > maximumAssetFiles)
    {
        addAssetError (report, "ASSET004", "assets",
                       "Skin package contains more than 512 asset files.");
        return std::nullopt;
    }

    std::vector<std::pair<juce::String, std::unique_ptr<juce::XmlElement>>> pendingSvgs;
    for (const auto& path : uniqueAssetFiles)
    {
        const auto extension = extensionOf (path);
        const auto bytes = package.readBinary (path, report);
        if (report.hasErrors()) return std::nullopt;

        if (isBitmapExtension (extension))
        {
            if (const auto dimensions = probeBitmapDimensions (bytes, extension))
            {
                const auto pixels = static_cast<int64_t> (dimensions->first)
                                  * dimensions->second;
                if (dimensions->first < 1 || dimensions->second < 1
                    || dimensions->first > maximumBitmapDimension
                    || dimensions->second > maximumBitmapDimension
                    || pixels > maximumBitmapBytes / 4)
                {
                    addAssetError (report, "ASSET003", path,
                                   "Bitmap header exceeds the per-image decode limit.");
                    return std::nullopt;
                }
            }
            auto image = juce::ImageFileFormat::loadFrom (bytes.getData(), bytes.getSize());
            if (! image.isValid())
            {
                addAssetError (report, "ASSET002", path,
                               "Bitmap format is unsupported or corrupt.");
                return std::nullopt;
            }
            const auto width = image.getWidth();
            const auto height = image.getHeight();
            const auto pixels = static_cast<int64_t> (width) * height;
            const auto decodedBytes = pixels * 4;
            if (width > maximumBitmapDimension || height > maximumBitmapDimension
                || decodedBytes > maximumBitmapBytes)
            {
                addAssetError (report, "ASSET003", path,
                               "Bitmap dimensions or decoded memory exceed the per-image limit.");
                return std::nullopt;
            }
            cache->stats.decodedPixels += pixels;
            cache->stats.decodedBytes += decodedBytes;
            if (cache->stats.decodedPixels > maximumPackagePixels
                || cache->stats.decodedBytes > maximumDecodedBytes)
            {
                addAssetError (report, "ASSET004", path,
                               "Skin bitmap totals exceed the package cache budget.");
                return std::nullopt;
            }
            cache->images.emplace (path, std::move (image));
            continue;
        }

        if (extension == ".svg")
        {
            const auto text = juce::String::fromUTF8 (
                static_cast<const char*> (bytes.getData()),
                static_cast<int> (bytes.getSize()));
            if (containsForbiddenXmlDeclaration (text))
            {
                addAssetError (report, "ASSET006", path,
                               "SVG DTD and entity declarations are disabled.");
                return std::nullopt;
            }
            const auto preflight = preflightSvgText (
                text, maximumPackageSvgPathCommands - cache->stats.svgPathCommands,
                report, path);
            if (! preflight) return std::nullopt;
            auto xml = juce::XmlDocument::parse (text);
            if (xml == nullptr || xml->getTagName() != "svg")
            {
                addAssetError (report, "ASSET006", path,
                               "SVG XML could not be parsed safely.");
                return std::nullopt;
            }
            int elements = 0;
            int commands = 0;
            std::set<juce::String> ids;
            std::set<juce::String> references;
            if (! validateSvgElement (
                    *xml, 1, elements, commands,
                    maximumPackageSvgPathCommands - cache->stats.svgPathCommands,
                                      ids, references, report, path))
                return std::nullopt;
            if (elements != preflight->elements || commands != preflight->pathCommands)
            {
                addAssetError (report, "ASSET006", path,
                               "SVG streaming preflight and parsed geometry disagree.");
                return std::nullopt;
            }
            for (const auto& reference : references)
                if (! ids.contains (reference))
                {
                    addAssetError (report, "ASSET009", path,
                                   "SVG local fragment does not resolve to an id: #"
                                       + reference + ".");
                    return std::nullopt;
                }
            cache->stats.svgElements += elements;
            cache->stats.svgPathCommands += commands;
            if (cache->stats.svgElements > maximumPackageSvgElements
                || cache->stats.svgPathCommands > maximumPackageSvgPathCommands)
            {
                addAssetError (report, "ASSET010", path,
                               "SVG package DOM or path-command budget is exceeded.");
                return std::nullopt;
            }
            pendingSvgs.emplace_back (path, std::move (xml));
            continue;
        }

        if (isFontExtension (extension))
        {
            if (static_cast<int64_t> (bytes.getSize()) > maximumFontBytes)
            {
                addAssetError (report, "ASSET005", path,
                               "Font exceeds the 12 MiB per-font limit.");
                return std::nullopt;
            }
            std::scoped_lock fontLock (packageFontMutex());
            auto typeface = juce::Typeface::createSystemTypefaceFor (
                bytes.getData(), bytes.getSize());
            const auto requested = std::find_if (
                referencedFonts.begin(), referencedFonts.end(), [&] (const auto& font)
                { return font.source == path; });
            if (typeface == nullptr)
            {
                if (requested == referencedFonts.end() || requested->families.isEmpty())
                {
                    addAssetError (report, "ASSET005", path,
                                   "Font could not be parsed and has no family fallback.");
                    return std::nullopt;
                }
                continue;
            }
            const auto family = normalisedFontFamily (typeface->getName());
            packageFontFamilies().insert (family);
            cache->typefaces.emplace (path, std::move (typeface));
            continue;
        }

        addAssetError (report, "ASSET002", path,
                       "Asset file extension is not supported by Skin API 1.");
        return std::nullopt;
    }

    for (auto& [path, xml] : pendingSvgs)
    {
        auto drawable = juce::Drawable::createFromSVG (*xml);
        if (drawable == nullptr)
        {
            addAssetError (report, "ASSET006", path,
                           "SVG could not be compiled into a drawable.");
            return std::nullopt;
        }
        cache->svgs.emplace (path,
            std::shared_ptr<const juce::Drawable> (drawable.release()));
    }

    {
        std::scoped_lock fontLock (packageFontMutex());
        for (const auto& font : referencedFonts)
        {
            juce::StringArray safeFamilies;
            for (const auto& family : font.families)
                if (! packageFontFamilies().contains (normalisedFontFamily (family)))
                    safeFamilies.add (family);

            juce::Typeface::Ptr packageTypeface;
            if (font.source.isNotEmpty())
                if (const auto found = cache->typefaces.find (font.source);
                    found != cache->typefaces.end())
                    packageTypeface = found->second;

            cache->resolvedFonts.push_back (SkinFontResolver::resolve (
                font, std::move (packageTypeface), safeFamilies));
        }
    }

    return cache;
}

const juce::Image* SkinAssetCache::findImage (juce::StringRef pathRef) const
{
    const auto found = images.find (juce::String (pathRef));
    return found == images.end() ? nullptr : &found->second;
}

std::shared_ptr<const juce::Drawable> SkinAssetCache::findSvg (juce::StringRef pathRef) const
{
    const auto found = svgs.find (juce::String (pathRef));
    return found == svgs.end() ? nullptr : found->second;
}

juce::Font SkinAssetCache::makeFont (const SkinFontValue& value, float height) const
{
    std::scoped_lock fontLock (packageFontMutex());
    const auto resolved = std::find_if (
        resolvedFonts.begin(), resolvedFonts.end(), [&] (const auto& face)
        { return face.matches (value); });
    if (resolved != resolvedFonts.end())
        return SkinFontResolver::makeFont (*resolved, height);
    return SkinFontResolver::makeFallbackFont (value, height);
}

const juce::String& SkinAssetCache::namespaceKey() const noexcept
{
    return cacheNamespace;
}

const SkinAssetCache::Statistics& SkinAssetCache::statistics() const noexcept
{
    return stats;
}
