#include <juce_core/juce_core.h>

namespace
{
class SkinVisualIsolationTests final : public juce::UnitTest
{
public:
    SkinVisualIsolationTests() : UnitTest ("SkinVisualIsolation", "skin") {}

    void runTest() override
    {
        const juce::File root (MOONWAKE_SOURCE_DIR);

        beginTest ("player surfaces contain no hard-coded visual colours");
        for (const auto& relativePath : {
                 "Source/ui/MainComponent.cpp",
                 "Source/ui/MainComponent.h",
                 "Source/ui/AlbumArtworkComponent.cpp",
                 "Source/ui/AlbumArtworkComponent.h",
                 "Source/ui/OutputStageComponent.cpp",
                 "Source/ui/OutputStageComponent.h",
                 "Source/ui/SkinEffectChainViewport.cpp",
                 "Source/ui/SkinEffectChainViewport.h",
                 "Source/ui/SkinTableListBox.cpp",
                 "Source/ui/SkinTableListBox.h",
                 "Source/ui/SkinLayoutHost.cpp",
                 "Source/ui/SkinLayoutHost.h",
                 "Source/ui/settings/AudioSettingsPanel.cpp",
                 "Source/ui/settings/AudioSettingsPanel.h",
                 "Source/ui/settings/PluginSettingsPanel.cpp",
                 "Source/ui/settings/PluginSettingsPanel.h",
                 "Source/ui/settings/SkinLibraryPanel.cpp",
                 "Source/ui/settings/SkinLibraryPanel.h" })
        {
            const auto file = root.getChildFile (relativePath);
            expect (file.existsAsFile(), relativePath);
            auto compact = file.loadFileAsString();
            compact = compact.removeCharacters (" \t\r\n");
            for (const auto& forbidden : {
                     "juce::Colour(", "juce::Colour::", "juce::Colours::",
                     "juce::ColourGradient", "juce::DropShadow(",
                     ".setColour(", ".setFont(", ".setGradientFill(",
                     ".fillAll(", ".fillRect(", ".fillRoundedRectangle(",
                     ".fillEllipse(", ".fillPath(", ".strokePath(",
                     ".drawRect(", ".drawRoundedRectangle(", ".drawEllipse(",
                     ".drawLine(", ".drawText(", ".drawFittedText(",
                     "juce::Label::paint(", "juce::Viewport::paint(" })
                expect (! compact.contains (forbidden),
                        juce::String (relativePath) + " contains " + forbidden);
        }

        beginTest ("legacy appearance and workspace layout types are absent");
        const juce::String oldAppearanceType = "Player" "LookAndFeel";
        const juce::String oldLayoutType = "Workspace" "Layout";
        expect (! root.getChildFile ("Source/ui/" + oldAppearanceType + ".h").exists());
        expect (! root.getChildFile ("Source/ui/" + oldAppearanceType + ".cpp").exists());
        expect (! root.getChildFile ("Source/ui/" + oldLayoutType + ".h").exists());
        expect (! root.getChildFile ("Source/ui/" + oldLayoutType + ".cpp").exists());
        expect (! root.getChildFile ("Tests/" + oldLayoutType + "Tests.cpp").exists());

        juce::Array<juce::File> sourceFiles;
        root.getChildFile ("Source").findChildFiles (
            sourceFiles, juce::File::findFiles, true, "*.cpp");
        root.getChildFile ("Source").findChildFiles (
            sourceFiles, juce::File::findFiles, true, "*.h");
        sourceFiles.add (root.getChildFile ("CMakeLists.txt"));
        for (const auto& file : sourceFiles)
        {
            const auto text = file.loadFileAsString();
            expect (! text.contains (oldAppearanceType),
                    file.getRelativePathFrom (root));
            expect (! text.contains (oldLayoutType),
                    file.getRelativePathFrom (root));
        }

        beginTest ("JUCE UI copy never relies on implicit non-ASCII narrow strings");
        juce::MemoryBlock mainSource;
        expect (root.getChildFile ("Source/ui/MainComponent.cpp")
                    .loadFileAsData (mainSource));
        const auto* bytes = static_cast<const unsigned char*> (mainSource.getData());
        bool containsNonAsciiByte = false;
        for (size_t index = 0; index < mainSource.getSize(); ++index)
            containsNonAsciiByte = containsNonAsciiByte || bytes[index] > 0x7f;
        expect (! containsNonAsciiByte,
                "MainComponent.cpp UI literals must remain ASCII; use an explicit "
                "JUCE Unicode conversion when a non-ASCII glyph is required.");
    }
};

SkinVisualIsolationTests skinVisualIsolationTests;
} // namespace
