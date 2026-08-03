#include "SkinSchemaValidator.h"

#include "SkinCompatibilityChecker.h"

#include <cstring>
#include <cmath>
#include <regex>
#include <set>

namespace
{
bool isSafePackagePath (const juce::String& value)
{
    if (value.isEmpty() || value.startsWithChar ('/') || value.containsChar ('\\'))
        return false;

    const auto segments = juce::StringArray::fromTokens (value, "/", {});
    if (segments.size() == 0 || value.contains ("//"))
        return false;

    for (const auto& segment : segments)
        if (segment.isEmpty() || segment == "." || segment == ".."
            || std::strlen (segment.toRawUTF8()) > 255)
            return false;

    return std::strlen (value.toRawUTF8()) <= 512;
}

class Validator
{
public:
    Validator (SkinSchemaDocument documentIn, const juce::var& rootSchemaIn,
               juce::String fileIn, SkinValidationReport& reportIn)
        : document (documentIn), rootSchema (rootSchemaIn), file (std::move (fileIn)),
          report (reportIn)
    {
    }

    bool validate (const juce::var& value, const juce::var& schema,
                   const juce::String& pointer)
    {
        if (! schema.isObject())
            return true;

        if (const auto ref = schema.getProperty ("$ref", {}).toString(); ref.isNotEmpty())
            return validate (value, resolveReference (ref), pointer);

        if (const auto oneOf = schema.getProperty ("oneOf", {}); oneOf.isArray())
        {
            int matches = 0;
            for (const auto& choice : *oneOf.getArray())
            {
                SkinValidationReport scratch;
                if (Validator (document, rootSchema, file, scratch).validate (value, choice, pointer))
                    ++matches;
            }
            if (matches != 1)
                return fail ("SCHEMA005", pointer,
                             "Value must match exactly one schema alternative.");
        }

        if (schema.getDynamicObject()->hasProperty ("const")
            && value != schema.getProperty ("const", {}))
            return fail ("SCHEMA003", pointer, "Value does not match the required constant.");

        if (const auto choices = schema.getProperty ("enum", {}); choices.isArray())
        {
            const auto found = std::any_of (choices.getArray()->begin(), choices.getArray()->end(),
                                            [&] (const auto& choice) { return value == choice; });
            if (! found)
                return fail ("SCHEMA003", pointer, "Value is not a recognised enum member.");
        }

        const auto type = schema.getProperty ("type", {}).toString();
        if (type.isNotEmpty() && ! matchesType (value, type))
            return fail (document == SkinSchemaDocument::manifest ? "MANIFEST002" : "SCHEMA002",
                         pointer, "JSON value has the wrong type.");

        if (value.isArray() && ! validateArray (value, schema, pointer))
            return false;
        if (value.isObject() && ! value.isArray()
            && ! validateObject (value, schema, pointer))
            return false;
        if (value.isString() && ! validateString (value.toString(), schema, pointer))
            return false;
        if ((value.isInt() || value.isInt64() || value.isDouble())
            && ! validateNumber (value, schema, pointer))
            return false;

        if (const auto allOf = schema.getProperty ("allOf", {}); allOf.isArray())
            for (const auto& constraint : *allOf.getArray())
                if (! validate (value, constraint, pointer))
                    return false;

        if (schema.getDynamicObject()->hasProperty ("if"))
        {
            SkinValidationReport scratch;
            if (Validator (document, rootSchema, file, scratch)
                    .validate (value, schema.getProperty ("if", {}), pointer)
                && schema.getDynamicObject()->hasProperty ("then")
                && ! validate (value, schema.getProperty ("then", {}), pointer))
                return false;
        }

        return true;
    }

private:
    bool validateObject (const juce::var& value, const juce::var& schema,
                         const juce::String& pointer)
    {
        const auto propertyCount = value.getDynamicObject()->getProperties().size();
        const auto minimumProperties = static_cast<int> (schema.getProperty ("minProperties", 0));
        const auto maximumProperties = static_cast<int> (
            schema.getProperty ("maxProperties", std::numeric_limits<int>::max()));
        if (propertyCount < minimumProperties || propertyCount > maximumProperties)
            return fail ("SCHEMA004", pointer,
                         "Object property count is outside the allowed range.");

        const auto required = schema.getProperty ("required", {});
        if (const auto* requiredItems = required.getArray())
            for (const auto& item : *requiredItems)
                if (! value.getDynamicObject()->hasProperty (item.toString()))
                    return fail (document == SkinSchemaDocument::manifest ? "MANIFEST001" : "SCHEMA004",
                                 pointer + "/" + escapePointer (item.toString()),
                                 "Required property is missing.");

        const auto properties = schema.getProperty ("properties", {});
        const auto additional = schema.getProperty ("additionalProperties", {});
        const auto allowAdditional = additional.isVoid() || ! additional.isBool()
                                  || static_cast<bool> (additional);

        const auto& values = value.getDynamicObject()->getProperties();
        const auto propertyNames = schema.getProperty ("propertyNames", {});
        for (int i = 0; i < values.size(); ++i)
        {
            const auto name = values.getName (i).toString();
            const auto childPointer = pointer + "/" + escapePointer (name);
            if (propertyNames.isObject()
                && ! validate (juce::var (name), propertyNames, childPointer))
                return false;
            if (properties.isObject() && properties.getDynamicObject()->hasProperty (name))
            {
                if (! validate (values.getValueAt (i), properties.getProperty (name, {}), childPointer))
                    return false;
            }
            else if (! allowAdditional)
            {
                return fail ("SCHEMA001", childPointer, "Unknown field in a closed object.");
            }
            else if (additional.isObject()
                     && ! validate (values.getValueAt (i), additional, childPointer))
            {
                return false;
            }
        }

        return true;
    }

    bool validateNumber (const juce::var& value, const juce::var& schema,
                         const juce::String& pointer)
    {
        const auto number = static_cast<double> (value);
        if (! std::isfinite (number))
            return fail ("SCHEMA004", pointer, "Number must be finite.");

        const auto has = [&] (const char* property)
        {
            return schema.getDynamicObject()->hasProperty (property);
        };
        if (has ("minimum") && number < static_cast<double> (schema.getProperty ("minimum", {})))
            return fail ("SCHEMA004", pointer, "Number is below its minimum.");
        if (has ("maximum") && number > static_cast<double> (schema.getProperty ("maximum", {})))
            return fail ("SCHEMA004", pointer, "Number is above its maximum.");
        if (has ("exclusiveMinimum")
            && number <= static_cast<double> (schema.getProperty ("exclusiveMinimum", {})))
            return fail ("SCHEMA004", pointer, "Number must be greater than its exclusive minimum.");
        if (has ("exclusiveMaximum")
            && number >= static_cast<double> (schema.getProperty ("exclusiveMaximum", {})))
            return fail ("SCHEMA004", pointer, "Number must be below its exclusive maximum.");
        if (has ("multipleOf"))
        {
            const auto multiple = static_cast<double> (schema.getProperty ("multipleOf", {}));
            if (multiple <= 0.0 || std::abs (number / multiple - std::round (number / multiple)) > 1.0e-9)
                return fail ("SCHEMA004", pointer, "Number is not a required multiple.");
        }
        return true;
    }

    bool validateArray (const juce::var& value, const juce::var& schema,
                        const juce::String& pointer)
    {
        const auto* values = value.getArray();
        jassert (values != nullptr);
        const auto minItems = static_cast<int> (schema.getProperty ("minItems", 0));
        const auto maxItems = static_cast<int> (schema.getProperty ("maxItems", std::numeric_limits<int>::max()));
        if (values->size() < minItems || values->size() > maxItems)
            return fail ("SCHEMA004", pointer, "Array size is outside the allowed range.");

        if (static_cast<bool> (schema.getProperty ("uniqueItems", false)))
        {
            std::set<juce::String> unique;
            for (const auto& item : *values)
                if (! unique.insert (juce::JSON::toString (item, false)).second)
                    return fail ("SCHEMA004", pointer, "Array items must be unique.");
        }

        const auto itemSchema = schema.getProperty ("items", {});
        if (itemSchema.isObject())
            for (int i = 0; i < values->size(); ++i)
            {
                if (! validate (values->getReference (i), itemSchema,
                                pointer + "/" + juce::String (i)))
                    return false;
            }

        return true;
    }

    bool validateString (const juce::String& value, const juce::var& schema,
                         const juce::String& pointer)
    {
        const auto minLength = static_cast<int> (schema.getProperty ("minLength", 0));
        const auto maxLength = static_cast<int> (schema.getProperty ("maxLength", std::numeric_limits<int>::max()));
        if (value.length() < minLength || value.length() > maxLength)
            return fail (constraintCode (pointer), pointer,
                         "String length is outside the allowed range.");

        const auto pattern = schema.getProperty ("pattern", {}).toString();
        if (pattern.isNotEmpty())
        {
            if (document == SkinSchemaDocument::manifest
                && (pointer == "/version" || pointer == "/minAppVersion"))
            {
                if (! SkinSemVersion::parse (value))
                    return fail ("MANIFEST004", pointer, "Version is not SemVer 2.0.");
                return true;
            }

            if (document == SkinSchemaDocument::manifest
                && pointer.startsWith ("/entrypoints/"))
            {
                if (! isSafePackagePath (value))
                    return fail ("MANIFEST005", pointer, "Entrypoint is not a safe package path.");
                return true;
            }

            try
            {
                const std::regex expression (pattern.toStdString(), std::regex::ECMAScript);
                const auto matches = pattern.startsWithChar ('^') && pattern.endsWithChar ('$')
                                   ? std::regex_match (value.toStdString(), expression)
                                   : std::regex_search (value.toStdString(), expression);
                if (! matches)
                    return fail (constraintCode (pointer), pointer,
                                 "String does not match the required format.");
            }
            catch (const std::regex_error&)
            {
                return fail ("SCHEMA004", pointer, "Application schema contains an invalid pattern.");
            }
        }

        return true;
    }

    bool matchesType (const juce::var& value, const juce::String& type) const
    {
        if (type == "object") return value.isObject() && ! value.isArray();
        if (type == "array") return value.isArray();
        if (type == "string") return value.isString();
        if (type == "boolean") return value.isBool();
        if (type == "integer") return value.isInt() || value.isInt64();
        if (type == "number") return value.isInt() || value.isInt64() || value.isDouble();
        return true;
    }

    juce::var resolveReference (const juce::String& reference) const
    {
        constexpr auto prefix = "#/$defs/";
        if (! reference.startsWith (prefix))
            return {};
        return rootSchema.getProperty ("$defs", {})
            .getProperty (reference.substring (static_cast<int> (std::strlen (prefix))), {});
    }

    juce::String constraintCode (const juce::String& pointer) const
    {
        if (document != SkinSchemaDocument::manifest)
            return "SCHEMA004";
        if (pointer == "/id")
            return "MANIFEST003";
        if (pointer == "/version" || pointer == "/minAppVersion")
            return "MANIFEST004";
        if (pointer.startsWith ("/entrypoints/"))
            return "MANIFEST005";
        return "SCHEMA004";
    }

    bool fail (const juce::String& code, const juce::String& pointer,
               const juce::String& message)
    {
        report.addError (code, file, pointer, message, "Check the Skin API 1 schema.");
        return false;
    }

    static juce::String escapePointer (juce::String value)
    {
        return value.replace ("~", "~0").replace ("/", "~1");
    }

    SkinSchemaDocument document;
    const juce::var& rootSchema;
    juce::String file;
    SkinValidationReport& report;
};
} // namespace

SkinValidatedDocument::SkinValidatedDocument (SkinSchemaDocument kindIn, juce::var valueIn)
    : documentKind (kindIn), documentValue (std::move (valueIn))
{
}

SkinSchemaDocument SkinValidatedDocument::kind() const
{
    return documentKind;
}

const juce::var& SkinValidatedDocument::value() const
{
    return documentValue;
}

std::optional<SkinValidatedDocument> SkinSchemaValidator::validate (
    SkinSchemaDocument document, const juce::var& value, juce::String file,
    SkinValidationReport& report)
{
    const auto schema = SkinSchemaRegistry::jsonSchema (document);
    if (! Validator (document, schema, std::move (file), report).validate (value, schema, {}))
        return std::nullopt;

    return SkinValidatedDocument (document, value.clone());
}
