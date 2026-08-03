#include "SkinTokenStore.h"

namespace
{
enum class TokenGroup { colours, metrics, durations, fonts, gradients, strings };
enum class VisitState { white, grey, black };

struct Declaration
{
    TokenGroup group;
    juce::var value;
    juce::String pointer;
};

struct CompiledTokens
{
    std::map<juce::String, SkinTokenValue> values;
    std::map<juce::String, juce::String> origins;
};

juce::String groupName (TokenGroup group)
{
    switch (group)
    {
        case TokenGroup::colours:   return "colors";
        case TokenGroup::metrics:   return "metrics";
        case TokenGroup::durations: return "durations";
        case TokenGroup::fonts:     return "fonts";
        case TokenGroup::gradients: return "gradients";
        case TokenGroup::strings:   return "strings";
    }
    return {};
}

std::optional<TokenGroup> groupForPath (const juce::String& path)
{
    const auto group = path.upToFirstOccurrenceOf (".", false, false);
    if (group == "colors") return TokenGroup::colours;
    if (group == "metrics") return TokenGroup::metrics;
    if (group == "durations") return TokenGroup::durations;
    if (group == "fonts") return TokenGroup::fonts;
    if (group == "gradients") return TokenGroup::gradients;
    if (group == "strings") return TokenGroup::strings;
    return std::nullopt;
}

juce::String escapePointer (juce::String value)
{
    return value.replace ("~", "~0").replace ("/", "~1");
}

bool isReference (const juce::var& value)
{
    return value.isObject() && ! value.isArray()
        && value.getDynamicObject()->hasProperty ("$ref");
}

class Compiler
{
public:
    Compiler (const SkinValidatedDocument& document,
              std::map<juce::String, SkinTokenValue> inherited,
              std::map<juce::String, juce::String> inheritedOrigins,
              juce::String originIn,
              SkinValidationReport& reportIn)
        : values (std::move (inherited)), origins (std::move (inheritedOrigins)),
          currentOrigin (std::move (originIn)), report (reportIn)
    {
        collect (document.value());
    }

    std::optional<CompiledTokens> run()
    {
        for (const auto& [path, ignored] : declarations)
        {
            juce::ignoreUnused (ignored);
            if (! resolve (path))
                return std::nullopt;
        }
        return CompiledTokens { std::move (values), std::move (origins) };
    }

private:
    void collect (const juce::var& root)
    {
        for (const auto group : { TokenGroup::colours, TokenGroup::metrics,
                                  TokenGroup::durations, TokenGroup::fonts,
                                  TokenGroup::gradients,
                                  TokenGroup::strings })
        {
            const auto name = groupName (group);
            const auto members = root.getProperty (name, {});
            if (! members.isObject() || members.isArray())
                continue;
            const auto& properties = members.getDynamicObject()->getProperties();
            for (int i = 0; i < properties.size(); ++i)
            {
                const auto key = properties.getName (i).toString();
                declarations.emplace (name + "." + key,
                                      Declaration { group, properties.getValueAt (i),
                                                    "/" + name + "/" + escapePointer (key) });
            }
        }
    }

    std::optional<SkinTokenValue> resolve (const juce::String& path)
    {
        if (const auto declaration = declarations.find (path);
            declaration != declarations.end())
        {
            const auto state = states[path];
            if (state == VisitState::grey)
            {
                fail ("TOKEN004", declaration->second.pointer,
                      "Token references form a cycle at " + path + ".");
                return std::nullopt;
            }
            if (state == VisitState::black)
                return values.at (path);

            states[path] = VisitState::grey;
            auto resolved = compileValue (declaration->second);
            if (! resolved)
                return std::nullopt;
            values.insert_or_assign (path, *resolved);
            auto origin = currentOrigin;
            if (isReference (declaration->second.value))
            {
                const auto referencedPath = declaration->second.value
                                                .getProperty ("$ref", {}).toString();
                if (const auto found = origins.find (referencedPath);
                    found != origins.end())
                    origin = found->second;
            }
            origins.insert_or_assign (path, std::move (origin));
            states[path] = VisitState::black;
            return resolved;
        }

        if (const auto inherited = values.find (path); inherited != values.end())
            return inherited->second;

        fail ("TOKEN003", {}, "Referenced token does not exist: " + path + ".");
        return std::nullopt;
    }

    std::optional<SkinTokenValue> resolveReference (const juce::var& reference,
                                                    TokenGroup expected,
                                                    const juce::String& pointer)
    {
        const auto path = reference.getProperty ("$ref", {}).toString();
        const auto actual = groupForPath (path);
        if (! actual || *actual != expected)
        {
            fail ("TOKEN005", pointer,
                  "Token reference crosses incompatible groups: " + path + ".");
            return std::nullopt;
        }
        return resolve (path);
    }

    std::optional<juce::Colour> compileColour (const juce::var& value,
                                               const juce::String& pointer)
    {
        if (isReference (value))
        {
            auto referenced = resolveReference (value, TokenGroup::colours, pointer);
            if (! referenced)
                return std::nullopt;
            if (const auto* colour = std::get_if<juce::Colour> (&*referenced))
                return *colour;
            fail ("TOKEN005", pointer, "Referenced token is not a colour.");
            return std::nullopt;
        }

        if (const auto colour = parseSkinCssColour (value.toString()))
            return colour;
        fail ("TOKEN002", pointer, "Colour is not #RRGGBB or #RRGGBBAA.");
        return std::nullopt;
    }

    std::optional<SkinTokenValue> compileValue (const Declaration& declaration)
    {
        if (isReference (declaration.value))
            return resolveReference (declaration.value, declaration.group, declaration.pointer);

        switch (declaration.group)
        {
            case TokenGroup::colours:
            {
                const auto colour = compileColour (declaration.value, declaration.pointer);
                return colour ? std::optional<SkinTokenValue> (*colour) : std::nullopt;
            }
            case TokenGroup::metrics:
                return SkinTokenValue (static_cast<double> (declaration.value));
            case TokenGroup::durations:
                return SkinTokenValue (static_cast<int64_t> (declaration.value));
            case TokenGroup::strings:
                return SkinTokenValue (declaration.value.toString());
            case TokenGroup::fonts:
                return compileFont (declaration);
            case TokenGroup::gradients:
                return compileGradient (declaration);
        }
        return std::nullopt;
    }

    std::optional<SkinTokenValue> compileFont (const Declaration& declaration)
    {
        SkinFontValue result;
        result.source = declaration.value.getProperty ("source", {}).toString();
        result.weight = static_cast<int> (declaration.value.getProperty ("weight", 400));
        result.italic = static_cast<bool> (declaration.value.getProperty ("italic", false));
        result.tracking = static_cast<float> (
            static_cast<double> (declaration.value.getProperty ("tracking", 0.0)));
        if (const auto* families = declaration.value.getProperty ("families", {}).getArray())
            for (const auto& family : *families)
                result.families.add (family.toString());
        if (result.families.isEmpty())
        {
            fail ("TOKEN007", declaration.pointer, "Font has no fallback family.");
            return std::nullopt;
        }
        return SkinTokenValue (std::move (result));
    }

    std::optional<SkinTokenValue> compileGradient (const Declaration& declaration)
    {
        SkinGradientValue result;
        const auto type = declaration.value.getProperty ("type", {}).toString();
        result.type = type == "radial" ? SkinGradientValue::Type::radial
                                       : SkinGradientValue::Type::linear;
        result.angle = numberProperty (declaration.value, "angle");
        result.centreX = numberProperty (declaration.value, "centerX", 0.5f);
        result.centreY = numberProperty (declaration.value, "centerY", 0.5f);
        result.radius = numberProperty (declaration.value, "radius", 1.0f);

        float previous = -1.0f;
        const auto* stops = declaration.value.getProperty ("stops", {}).getArray();
        jassert (stops != nullptr);
        if (stops == nullptr)
            return std::nullopt;
        for (int i = 0; i < stops->size(); ++i)
        {
            const auto& stop = stops->getReference (i);
            const auto position = numberProperty (stop, "position");
            if (position < previous)
            {
                fail ("TOKEN006", declaration.pointer + "/stops/" + juce::String (i),
                      "Gradient stops must be sorted by non-decreasing position.");
                return std::nullopt;
            }
            const auto colour = compileColour (stop.getProperty ("color", {}),
                                               declaration.pointer + "/stops/"
                                                   + juce::String (i) + "/color");
            if (! colour)
                return std::nullopt;
            result.stops.push_back ({ position, *colour });
            previous = position;
        }
        return SkinTokenValue (std::move (result));
    }

    static float numberProperty (const juce::var& object, const char* property,
                                 float fallback = 0.0f)
    {
        const auto value = object.getProperty (property, fallback);
        return static_cast<float> (static_cast<double> (value));
    }

    void fail (juce::String code, juce::String pointer, juce::String message)
    {
        report.addError (std::move (code), "tokens.json", std::move (pointer),
                         std::move (message), "Correct the token declaration or reference.");
    }

    std::map<juce::String, Declaration> declarations;
    std::map<juce::String, VisitState> states;
    std::map<juce::String, SkinTokenValue> values;
    std::map<juce::String, juce::String> origins;
    juce::String currentOrigin;
    SkinValidationReport& report;
};
} // namespace

std::optional<SkinTokenStore> SkinTokenStore::compile (
    const SkinValidatedDocument& child, const SkinTokenStore* parent,
    SkinValidationReport& report, juce::String origin)
{
    if (child.kind() != SkinSchemaDocument::tokens)
    {
        report.addError ("TOKEN002", "tokens.json", {},
                         "SkinTokenStore requires a validated tokens document.");
        return std::nullopt;
    }

    auto resolved = Compiler (
        child, parent != nullptr ? parent->values : decltype (values) {},
        parent != nullptr ? parent->origins : decltype (origins) {},
        std::move (origin), report).run();
    if (! resolved)
        return std::nullopt;
    SkinTokenStore result;
    result.values = std::move (resolved->values);
    result.origins = std::move (resolved->origins);
    return result;
}

bool SkinTokenStore::contains (const juce::String& key) const
{
    return values.contains (key);
}

juce::String SkinTokenStore::originOf (const juce::String& key) const
{
    const auto found = origins.find (key);
    return found == origins.end() ? juce::String() : found->second;
}
