#pragma once

#include "PlayerComponentCatalog.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace SkinTool
{
enum class Command
{
    help,
    validate,
    pack,
    layout,
    snapshot,
    schema,
    verifyDocs
};

enum class SnapshotState
{
    representative,
    normal,
    hover,
    pressed,
    selected,
    focused,
    disabled
};

struct Options
{
    Command command = Command::help;
    juce::File input;
    juce::File output;
    juce::File outputDirectory;
    juce::File guide;
    juce::File example;
    std::optional<SkinLayoutEntry> workspace;
    SnapshotState snapshotState = SnapshotState::representative;
    int width = 0;
    int height = 0;
    bool warningsAsErrors = false;
    bool force = false;
};

struct ParseResult
{
    std::optional<Options> options;
    juce::String error;
};

struct ExecutionResult
{
    int exitCode = 0;
    juce::String standardOutput;
    juce::String standardError;
};

[[nodiscard]] juce::String helpText();
[[nodiscard]] ParseResult parse (const juce::StringArray& arguments);
[[nodiscard]] ExecutionResult execute (const Options&);
[[nodiscard]] ExecutionResult run (const juce::StringArray& arguments);
}
