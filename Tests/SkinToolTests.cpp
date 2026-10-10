#include "skin/SkinSchemaRegistry.h"
#include "skin/PlayerComponentCatalog.h"
#include "skin/SkinToolCommands.h"

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : directory (juce::File::getCurrentWorkingDirectory().getChildFile ("build")
                         .getChildFile ("moonwake-skin-tool-test-"
                                        + juce::Uuid().toString()))
    {
        const auto result = directory.createDirectory();
        jassert (result.wasOk());
    }

    ~TemporaryDirectory() { directory.deleteRecursively(); }

    juce::File directory;
};

juce::File fixture()
{
    return juce::File (MOONWAKE_SOURCE_DIR)
        .getChildFile ("Tests/fixtures/skins/third-party-complete");
}

SkinTool::ExecutionResult run (std::initializer_list<const char*> arguments)
{
    juce::StringArray values;
    for (const auto* argument : arguments) values.add (argument);
    return SkinTool::run (values);
}

bool setComponentHeight (juce::var node, const juce::String& componentId,
                         int height)
{
    if (! node.isObject() || node.isArray()) return false;
    if (node.getProperty ("type", {}).toString() == "component"
        && node.getProperty ("component", {}).toString() == componentId)
    {
        node.getDynamicObject()->setProperty ("height", height);
        node.getDynamicObject()->setProperty ("minHeight", height);
        return true;
    }
    const auto children = node.getProperty ("children", {});
    if (const auto* values = children.getArray())
        for (const auto& child : *values)
            if (setComponentHeight (child, componentId, height)) return true;
    const auto child = node.getProperty ("child", {});
    return setComponentHeight (child, componentId, height);
}

void offsetFocusOrders (juce::var node, int offset)
{
    if (! node.isObject() || node.isArray()) return;
    const auto focusOrder = static_cast<int> (node.getProperty ("focusOrder", 0));
    if (focusOrder > 0)
        node.getDynamicObject()->setProperty ("focusOrder", focusOrder + offset);
    if (const auto* children = node.getProperty ("children", {}).getArray())
        for (const auto& child : *children)
            offsetFocusOrders (child, offset);
    offsetFocusOrders (node.getProperty ("child", {}), offset);
}

void suffixNodeIds (juce::var node, const juce::String& suffix)
{
    if (! node.isObject()) return;
    node.getDynamicObject()->setProperty ("id", node.getProperty ("id", {}).toString() + suffix);
    if (const auto* children = node.getProperty ("children", {}).getArray())
        for (const auto& child : *children) suffixNodeIds (child, suffix);
    suffixNodeIds (node.getProperty ("child", {}), suffix);
}

bool wrapComponentInHiddenParent (juce::var node,
                                  const juce::String& componentId,
                                  int hiddenFocusOrder)
{
    if (! node.isObject() || node.isArray()) return false;
    auto childrenValue = node.getProperty ("children", {});
    if (auto* children = childrenValue.getArray())
        for (int index = 0; index < children->size(); ++index)
        {
            const auto child = children->getReference (index);
            if (child.getProperty ("type", {}).toString() == "component"
                && child.getProperty ("component", {}).toString() == componentId)
            {
                child.getDynamicObject()->setProperty ("focusOrder",
                                                       hiddenFocusOrder);
                juce::Array<juce::var> wrappedChildren;
                wrappedChildren.add (child);
                auto wrapper = juce::var (new juce::DynamicObject());
                auto* object = wrapper.getDynamicObject();
                object->setProperty ("id", "hidden-focus-parent");
                object->setProperty ("type", "row");
                object->setProperty ("visible", false);
                object->setProperty ("width", 88);
                object->setProperty ("height", 40);
                object->setProperty ("children", wrappedChildren);
                children->set (index, wrapper);
                return true;
            }
            if (wrapComponentInHiddenParent (child, componentId,
                                             hiddenFocusOrder))
                return true;
        }
    return wrapComponentInHiddenParent (node.getProperty ("child", {}),
                                        componentId, hiddenFocusOrder);
}

juce::String guideForLayout (const juce::var& layout)
{
    return "# Test guide\n\n"
           "<!-- skin-doc-test: schema=layout route=listening semantics=complete -->\n"
           "~~~json\n" + juce::JSON::toString (layout, true) + "\n~~~\n";
}

class SkinToolTests final : public juce::UnitTest
{
public:
    SkinToolTests() : juce::UnitTest ("SkinTool") {}

    void runTest() override
    {
        beginTest ("shipped Settings roots keep every control in unique visual focus order");
        for (const auto* path : { "Resources/Skins/Default/layouts/settings.json",
                                  "Resources/Skins/Foundation/layouts/settings.json",
                                  "docs/skin-api/examples/CompleteThirdParty/layouts/settings.json" })
        {
            const auto document = juce::JSON::parse (juce::File (MOONWAKE_SOURCE_DIR).getChildFile (path));
            expect (document.isObject(), path);
            juce::Array<juce::var> roots;
            roots.add (document.getProperty ("root", {}));
            if (const auto* breakpoints = document.getProperty ("breakpoints", {}).getArray())
                for (const auto& breakpoint : *breakpoints)
                    if (breakpoint.hasProperty ("root")) roots.add (breakpoint.getProperty ("root", {}));
            for (const auto& root : roots)
            {
                int previous = 0;
                juce::StringArray musicOrder;
                const auto inspect = [&] (auto&& self, const juce::var& node) -> void
                {
                    if (! node.isObject()) return;
                    const auto id = node.getProperty ("component", {}).toString();
                    if (const auto* descriptor = PlayerComponentCatalog::find (id);
                        descriptor != nullptr && descriptor->interactive)
                    {
                        const auto order = static_cast<int> (node.getProperty ("focusOrder", 0));
                        expect (order > previous, juce::String (path) + ":" + id);
                        previous = order;
                        if (id.startsWith ("settings.music.")) musicOrder.add (id);
                    }
                    if (const auto* children = node.getProperty ("children", {}).getArray())
                        for (const auto& child : *children) self (self, child);
                    self (self, node.getProperty ("child", {}));
                };
                inspect (inspect, root);
                expect (musicOrder == juce::StringArray { "settings.music.directories", "settings.music.add",
                    "settings.music.scan", "settings.music.cancel", "settings.music.remove", "settings.music.automatic" });
            }
        }

        beginTest ("parser rejects incomplete and unknown commands deterministically");
        {
            const auto help = run ({});
            expectEquals (help.exitCode, 0);
            expect (help.standardOutput.contains ("validate <path>"));
            expectEquals (help.standardOutput, SkinTool::helpText());

            const auto unknown = run ({ "unknown" });
            expectEquals (unknown.exitCode, 2);
            expect (unknown.standardError.contains ("TOOL001"), unknown.standardError);
            const auto missing = run ({ "validate" });
            expectEquals (missing.exitCode, 2);
            expect (missing.standardError.contains ("TOOL002"), missing.standardError);
            expectEquals (run ({ "validate", "skin", "--wat" }).exitCode, 2);
            expectEquals (run ({ "layout", "skin", "--workspace", "mix",
                                 "--size", "1240x800" }).exitCode, 2);
            expectEquals (run ({ "layout", "skin", "--workspace", "listening",
                                 "--size", "12-by-8" }).exitCode, 2);
            expectEquals (run ({ "layout", "skin", "--workspace", "effects",
                                 "--size", "1240x800", "--theme", "effects" }).exitCode, 2);
            expectEquals (run ({ "layout", "skin", "--workspace", "effects",
                                 "--size", "1240x800", "--theme" }).exitCode, 2);
            expectEquals (run ({ "layout", "skin", "--workspace", "effects",
                                 "--size", "1240x800", "--theme", "studio",
                                 "--theme", "listening" }).exitCode, 2);
            expectEquals (run ({ "validate", "skin", "--theme", "studio" }).exitCode, 2);
            expectEquals (run ({ "validate", "skin", "--queue-open" }).exitCode, 2);
            expectEquals (run ({ "layout", "skin", "--workspace", "listening", "--size", "1040x700",
                                 "--queue-open", "--queue-open" }).exitCode, 2);
            const auto openedQueue = SkinTool::parse ({ "layout", "skin", "--workspace", "listening", "--size", "1040x700", "--queue-open" });
            expect (openedQueue.options && openedQueue.options->queueOpen);
            const auto lyrics = SkinTool::parse ({ "layout", "skin", "--workspace", "immersive", "--size", "1240x800", "--lyrics-open" });
            expect (lyrics.options && lyrics.options->lyricsOpen && lyrics.options->workspace == SkinLayoutEntry::immersive);
            const auto mini = SkinTool::parse ({ "layout", "skin", "--workspace", "mini", "--size", "380x170" });
            expect (mini.options && mini.options->workspace == SkinLayoutEntry::mini);
            expectEquals (run ({ "validate", "skin", "--lyrics-open" }).exitCode, 2);

            expectEquals (run ({ "snapshot", "skin", "--workspace", "listening",
                                 "--size", "1240x800" }).exitCode, 2);
            expectEquals (run ({ "snapshot", "skin", "--workspace", "listening",
                                 "--size", "1240x800", "--output", "out.png",
                                 "--state", "glowing" }).exitCode, 2);
            expectEquals (run ({ "schema" }).exitCode, 2);
            expectEquals (run ({ "verify-docs", "--guide", "guide.md" }).exitCode, 2);

            const auto warnings = SkinTool::parse (
                { "validate", fixture().getFullPathName(), "--warnings-as-errors" });
            expect (warnings.options.has_value());
            expect (warnings.options && warnings.options->warningsAsErrors);

            const auto schema = SkinTool::parse ({ "schema", "--output-dir", "schemas" });
            expect (schema.options.has_value());
            expect (schema.options && schema.options->command == SkinTool::Command::schema);

            const auto docs = SkinTool::parse (
                { "verify-docs", "--guide", "guide.md", "--example", "example" });
            expect (docs.options.has_value());
            expect (docs.options && docs.options->command == SkinTool::Command::verifyDocs);
        }

        beginTest ("validate and layout use the production compiler and layout engine");
        {
            const auto validation = run (
                { "validate", fixture().getFullPathName().toRawUTF8(),
                  "--warnings-as-errors" });
            expectEquals (validation.exitCode, 0, validation.standardError);
            expect (validation.standardOutput.contains (
                "com.example.complete-third-party"));

            const auto layout = run (
                { "layout", fixture().getFullPathName().toRawUTF8(),
                  "--workspace", "listening", "--size", "1240x800" });
            expectEquals (layout.exitCode, 0, layout.standardError);
            expect (layout.standardOutput.contains ("transport.seek "));
            expect (layout.standardOutput.contains ("library.add "));
            expect (layout.standardOutput.contains ("transport.volume "));

        }

        beginTest ("library routes use the current workspace and remain optional for old skins");
        {
            const auto example = juce::File (MOONWAKE_SOURCE_DIR)
                .getChildFile ("docs/skin-api/examples/CompleteThirdParty");
            for (const auto* page : { "library", "collection" })
            {
                const auto parsed = SkinTool::parse ({ "layout", example.getFullPathName(),
                    "--workspace", page, "--size", "1040x700", "--theme", "studio" });
                expect (parsed.options.has_value());
                if (parsed.options)
                {
                    expect (parsed.options->workspace == skinLayoutEntryFromName (page));
                    expect (parsed.options->theme == WorkspaceMode::studio);
                }
                const auto layout = run ({ "layout", example.getFullPathName().toRawUTF8(),
                    "--workspace", page, "--size", "1040x700", "--theme", "studio" });
                expectEquals (layout.exitCode, 0, layout.standardError);
                expect (layout.standardOutput.contains (juce::String (page) == "library" ? "library.grid " : "library.tracks "));
                const auto legacy = run ({ "layout", fixture().getFullPathName().toRawUTF8(),
                    "--workspace", page, "--size", "1040x700" });
                expect (legacy.exitCode != 0);
            }
        }

        beginTest ("pack output is reproducible and protected from accidental overwrite");
        {
            TemporaryDirectory temporary;
            const auto first = temporary.directory.getChildFile ("first.moonwake-skin");
            const auto second = temporary.directory.getChildFile ("second.moonwake-skin");
            auto packed = run (
                { "pack", fixture().getFullPathName().toRawUTF8(), "--output",
                  first.getFullPathName().toRawUTF8() });
            expectEquals (packed.exitCode, 0, packed.standardError);
            packed = run (
                { "pack", fixture().getFullPathName().toRawUTF8(), "--output",
                  second.getFullPathName().toRawUTF8() });
            expectEquals (packed.exitCode, 0, packed.standardError);
            juce::MemoryBlock firstBytes, secondBytes;
            expect (first.loadFileAsData (firstBytes));
            expect (second.loadFileAsData (secondBytes));
            expect (firstBytes == secondBytes);
            const auto exists = run ({ "pack", fixture().getFullPathName().toRawUTF8(),
                                       "--output", first.getFullPathName().toRawUTF8() });
            expectEquals (exists.exitCode, 1);
            expect (exists.standardError.contains ("TOOL003"), exists.standardError);
            expectEquals (run ({ "pack", fixture().getFullPathName().toRawUTF8(),
                                 "--output", first.getFullPathName().toRawUTF8(),
                                 "--force" }).exitCode, 0);
            expectEquals (run ({ "validate", first.getFullPathName().toRawUTF8() }).exitCode, 0);

            const auto copiedInput = temporary.directory.getChildFile ("skin-input");
            const auto inputAlias = temporary.directory.getChildFile ("skin-alias");
            expect (fixture().copyDirectoryTo (copiedInput));
            expect (copiedInput.createSymbolicLink (inputAlias, false));
            const auto aliasedOutput = inputAlias.getChildFile ("inside.moonwake-skin");
            const auto unsafe = run (
                { "pack", copiedInput.getFullPathName().toRawUTF8(), "--output",
                  aliasedOutput.getFullPathName().toRawUTF8() });
            expectEquals (unsafe.exitCode, 1);
            expect (unsafe.standardError.contains ("TOOL007"), unsafe.standardError);
            expect (! copiedInput.getChildFile ("inside.moonwake-skin").exists());

            const auto wrongInput = run (
                { "pack", fixture().getChildFile ("manifest.json")
                              .getFullPathName().toRawUTF8(),
                  "--output", temporary.directory.getChildFile ("wrong.moonwake-skin")
                                  .getFullPathName().toRawUTF8() });
            expectEquals (wrongInput.exitCode, 1);
            expect (wrongInput.standardError.contains ("TOOL005"),
                    wrongInput.standardError);

            const auto blocker = temporary.directory.getChildFile ("blocker");
            expect (blocker.replaceWithText ("not a directory"));
            const auto writeFailure = run (
                { "pack", fixture().getFullPathName().toRawUTF8(), "--output",
                  blocker.getChildFile ("failed.moonwake-skin")
                      .getFullPathName().toRawUTF8() });
            expectEquals (writeFailure.exitCode, 1);
            expect (writeFailure.standardError.contains ("TOOL004"),
                    writeFailure.standardError);
        }

        beginTest ("shared-panel layout and snapshot select the requested workspace theme");
        {
            TemporaryDirectory temporary;
            const auto skin = temporary.directory.getChildFile ("themed-skin");
            expect (fixture().copyDirectoryTo (skin));
            const auto file = skin.getChildFile ("layouts/effects.json");
            auto document = juce::JSON::parse (file);
            auto root = document.getProperty ("root", {}).clone();
            suffixNodeIds (root, "-studio");
            root.getDynamicObject()->setProperty ("padding", 28);
            auto condition = juce::var (new juce::DynamicObject());
            condition.getDynamicObject()->setProperty ("workspace", "studio");
            auto breakpoint = juce::var (new juce::DynamicObject());
            breakpoint.getDynamicObject()->setProperty ("when", condition);
            breakpoint.getDynamicObject()->setProperty ("root", root);
            document.getDynamicObject()->setProperty ("breakpoints", juce::Array<juce::var> { breakpoint });
            expect (file.replaceWithText (juce::JSON::toString (document, true)));
            const auto path = skin.getFullPathName();
            const auto base = run ({ "layout", path.toRawUTF8(), "--workspace", "effects", "--size", "1240x800" });
            const auto listening = run ({ "layout", path.toRawUTF8(), "--workspace", "effects", "--size", "1240x800",
                                         "--theme", "listening" });
            const auto studio = run ({ "layout", path.toRawUTF8(), "--workspace", "effects", "--size", "1240x800",
                                      "--theme", "studio" });
            expectEquals (base.exitCode, 0, base.standardError);
            expectEquals (listening.exitCode, 0, listening.standardError);
            expectEquals (studio.exitCode, 0, studio.standardError);
            expectEquals (base.standardOutput, listening.standardOutput);
            expect (studio.standardOutput != listening.standardOutput);
            juce::MemoryBlock images[2];
            int index = 0;
            for (const auto* theme : { "studio", "listening" })
            {
                const auto output = temporary.directory.getChildFile (juce::String (theme) + ".png");
                const auto snapshot = run ({ "snapshot", path.toRawUTF8(), "--workspace", "effects", "--size", "1240x800",
                                            "--theme", theme, "--output", output.getFullPathName().toRawUTF8() });
                expectEquals (snapshot.exitCode, 0, snapshot.standardError);
                expect (output.loadFileAsData (images[index++]));
            }
            expect (images[0] != images[1]);
            auto styles = juce::var (new juce::DynamicObject());
            styles.getDynamicObject()->setProperty ("popupMenu", "popupMenu.missing");
            document.getDynamicObject()->setProperty ("ancillaryStyles", styles);
            expect (file.replaceWithText (juce::JSON::toString (document, true)));
            const auto missingStyle = run ({ "validate", path.toRawUTF8() });
            expectEquals (missingStyle.exitCode, 1, missingStyle.standardError);
            expect (missingStyle.standardError.contains ("LAYOUT013"), missingStyle.standardError);
        }

        beginTest ("snapshot renders a deterministic-size PNG without an audio backend");
        {
            TemporaryDirectory temporary;
            const auto output = temporary.directory.getChildFile ("listening.png");
            const auto snapshot = run (
                { "snapshot", fixture().getFullPathName().toRawUTF8(),
                  "--workspace", "listening", "--size", "1240x800", "--output",
                  output.getFullPathName().toRawUTF8() });
            expectEquals (snapshot.exitCode, 0, snapshot.standardError);
            const auto image = juce::ImageFileFormat::loadFrom (output);
            expect (image.isValid());
            expectEquals (image.getWidth(), 1240);
            expectEquals (image.getHeight(), 800);
            const auto exists = run (
                { "snapshot", fixture().getFullPathName().toRawUTF8(),
                  "--workspace", "listening", "--size", "1240x800", "--output",
                  output.getFullPathName().toRawUTF8() });
            expectEquals (exists.exitCode, 1);
            expect (exists.standardError.contains ("TOOL003"), exists.standardError);

            const auto focused = temporary.directory.getChildFile ("focused.png");
            expectEquals (run (
                { "snapshot", fixture().getFullPathName().toRawUTF8(),
                  "--workspace", "listening", "--size", "1240x800", "--output",
                  focused.getFullPathName().toRawUTF8(), "--state", "focused" }).exitCode, 0);
            expect (juce::ImageFileFormat::loadFrom (focused).isValid());
            juce::MemoryBlock representativeBytes, focusedBytes;
            expect (output.loadFileAsData (representativeBytes));
            expect (focused.loadFileAsData (focusedBytes));
            expect (representativeBytes != focusedBytes);

            const auto shiftedSkin = temporary.directory.getChildFile ("shifted-skin");
            expect (fixture().copyDirectoryTo (shiftedSkin));
            const auto shiftedLayout = shiftedSkin.getChildFile ("layouts/listening.json");
            juce::var shiftedJson;
            expect (juce::JSON::parse (shiftedLayout.loadFileAsString(), shiftedJson).wasOk());
            offsetFocusOrders (shiftedJson.getProperty ("root", {}), 100);
            expect (shiftedLayout.replaceWithText (juce::JSON::toString (shiftedJson, true)));
            const auto shiftedOutput = temporary.directory.getChildFile ("shifted.png");
            const auto shiftedSnapshot = run (
                { "snapshot", shiftedSkin.getFullPathName().toRawUTF8(),
                  "--workspace", "listening", "--size", "1240x800", "--output",
                  shiftedOutput.getFullPathName().toRawUTF8() });
            expectEquals (shiftedSnapshot.exitCode, 0, shiftedSnapshot.standardError);
            juce::MemoryBlock shiftedBytes;
            expect (shiftedOutput.loadFileAsData (shiftedBytes));
            expect (representativeBytes == shiftedBytes,
                    "Representative focus must follow the minimum positive focusOrder");

            juce::MemoryBlock hiddenFocusReference;
            for (const auto hiddenFocusOrder : { 1, 1000 })
            {
                const auto hiddenSkin = temporary.directory.getChildFile (
                    "hidden-skin-" + juce::String (hiddenFocusOrder));
                expect (fixture().copyDirectoryTo (hiddenSkin));
                const auto hiddenLayout = hiddenSkin.getChildFile (
                    "layouts/listening.json");
                juce::var hiddenJson;
                expect (juce::JSON::parse (hiddenLayout.loadFileAsString(),
                                           hiddenJson).wasOk());
                offsetFocusOrders (hiddenJson.getProperty ("root", {}), 100);
                expect (wrapComponentInHiddenParent (
                    hiddenJson.getProperty ("root", {}), "effects.open",
                    hiddenFocusOrder));
                expect (hiddenLayout.replaceWithText (
                    juce::JSON::toString (hiddenJson, true)));
                const auto hiddenOutput = temporary.directory.getChildFile (
                    "hidden-" + juce::String (hiddenFocusOrder) + ".png");
                const auto hiddenSnapshot = run (
                    { "snapshot", hiddenSkin.getFullPathName().toRawUTF8(),
                      "--workspace", "listening", "--size", "1240x800",
                      "--output", hiddenOutput.getFullPathName().toRawUTF8() });
                expectEquals (hiddenSnapshot.exitCode, 0,
                              hiddenSnapshot.standardError);
                juce::MemoryBlock hiddenBytes;
                expect (hiddenOutput.loadFileAsData (hiddenBytes));
                if (hiddenFocusReference.isEmpty())
                    hiddenFocusReference = hiddenBytes;
                else
                    expect (hiddenFocusReference == hiddenBytes,
                            "Invisible parent subtrees must not consume representative focus");
            }
        }

        beginTest ("schema writes all five registry documents with overwrite provenance");
        {
            TemporaryDirectory temporary;
            const auto output = temporary.directory.getChildFile ("schemas");
            auto generated = run ({ "schema", "--output-dir",
                                    output.getFullPathName().toRawUTF8() });
            expectEquals (generated.exitCode, 0, generated.standardError);
            const auto serialised = SkinSchemaRegistry::serialisedSchemas();
            expectEquals (static_cast<int> (serialised.size()), 5);
            for (const auto& name : { "manifest", "tokens", "component-index",
                                      "layout", "render" })
            {
                const auto file = output.getChildFile (juce::String (name)
                                                       + ".schema.json");
                expect (file.existsAsFile());
                const auto value = juce::JSON::parse (file);
                expectEquals (value.getProperty ("x-generatedBy", {}).toString(),
                              juce::String ("Moonwake SkinSchemaRegistry"));
                expectEquals (static_cast<int> (value.getProperty ("x-skinApi", 0)), 1);
            }
            generated = run ({ "schema", "--output-dir",
                               output.getFullPathName().toRawUTF8() });
            expectEquals (generated.exitCode, 0, generated.standardError);

            const auto protectedDirectory = temporary.directory.getChildFile ("protected");
            expect (protectedDirectory.createDirectory());
            expect (protectedDirectory.getChildFile ("manifest.schema.json")
                        .replaceWithText ("{\"owner\":\"third-party\"}"));
            const auto protectedResult = run (
                { "schema", "--output-dir",
                  protectedDirectory.getFullPathName().toRawUTF8() });
            expectEquals (protectedResult.exitCode, 1);
            expect (protectedResult.standardError.contains ("TOOL003"),
                    protectedResult.standardError);
            expectEquals (run ({ "schema", "--output-dir",
                                 protectedDirectory.getFullPathName().toRawUTF8(),
                                 "--force" }).exitCode, 0);
        }

        beginTest ("verify-docs checks JSON fences, complete semantics and the example");
        {
            TemporaryDirectory temporary;
            juce::var listening;
            const auto parseResult = juce::JSON::parse (
                fixture().getChildFile ("layouts/listening.json").loadFileAsString(),
                listening);
            expect (parseResult.wasOk());
            const auto guide = temporary.directory.getChildFile ("guide.md");
            expect (guide.replaceWithText (guideForLayout (listening)));
            auto verified = run (
                { "verify-docs", "--guide", guide.getFullPathName().toRawUTF8(),
                  "--example", fixture().getFullPathName().toRawUTF8() });
            expectEquals (verified.exitCode, 0, verified.standardError);
            expect (verified.standardOutput.contains ("1 annotated"));

            expect (setComponentHeight (listening.getProperty ("root", {}),
                                        "transport.seek", 24));
            expect (guide.replaceWithText (guideForLayout (listening)));
            verified = run (
                { "verify-docs", "--guide", guide.getFullPathName().toRawUTF8(),
                  "--example", fixture().getFullPathName().toRawUTF8() });
            expectEquals (verified.exitCode, 1);
            expect (verified.standardError.contains ("LAYOUT015"),
                    verified.standardError);

            expect (guide.replaceWithText ("~~~json\n{ not-json }\n~~~\n"));
            verified = run (
                { "verify-docs", "--guide", guide.getFullPathName().toRawUTF8(),
                  "--example", fixture().getFullPathName().toRawUTF8() });
            expectEquals (verified.exitCode, 1);
            expect (verified.standardError.contains ("/line/2"),
                    verified.standardError);
            expect (verified.standardError.contains ("TOOL008"),
                    verified.standardError);

            expect (guide.replaceWithText (
                "<!-- skin-doc-test: schema=layout route=unknown semantics=complete -->\n"
                "~~~json\n{}\n~~~\n"));
            verified = run (
                { "verify-docs", "--guide", guide.getFullPathName().toRawUTF8(),
                  "--example", fixture().getFullPathName().toRawUTF8() });
            expectEquals (verified.exitCode, 1);
            expect (verified.standardError.contains ("marker is malformed"),
                    verified.standardError);

            expect (guide.replaceWithText (
                "<!-- skin-doc-test: schema=layout route=listening semantics=complete -->\n"
                "\n~~~json\n{}\n~~~\n"));
            verified = run (
                { "verify-docs", "--guide", guide.getFullPathName().toRawUTF8(),
                  "--example", fixture().getFullPathName().toRawUTF8() });
            expectEquals (verified.exitCode, 1);
            expect (verified.standardError.contains ("immediately followed"),
                    verified.standardError);
        }
    }
};

SkinToolTests skinToolTests;
} // namespace
