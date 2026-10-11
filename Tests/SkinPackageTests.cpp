#include "skin/SkinPackage.h"

#include <juce_core/juce_core.h>

#if JUCE_MAC || JUCE_LINUX
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : directory (juce::File::getCurrentWorkingDirectory().getChildFile ("build")
                         .getChildFile ("moonwake-skin-package-test-"
                                        + juce::Uuid().toString()))
    {
        const auto result = directory.createDirectory();
        jassert (result.wasOk());
    }

    ~TemporaryDirectory()
    {
        directory.deleteRecursively();
    }

    juce::File directory;
};

juce::File writeTextFile (const juce::File& root, const juce::String& path,
                          const juce::String& text)
{
    const auto file = root.getChildFile (path);
    file.getParentDirectory().createDirectory();
    file.replaceWithText (text);
    return file;
}

juce::File buildZip (const juce::File& target,
                     const std::vector<std::pair<juce::String, juce::String>>& entries)
{
    juce::ZipFile::Builder builder;
    const auto normalisedTimestamp = juce::Time (1980, 0, 1, 0, 0, 0, 0, true);
    for (const auto& [path, contents] : entries)
        builder.addEntry (new juce::MemoryInputStream (contents.toRawUTF8(),
                                                       contents.getNumBytesAsUTF8(), false),
                          9, path, normalisedTimestamp);

    juce::FileOutputStream output (target);
    builder.writeToStream (output, nullptr);
    output.flush();
    return target;
}

size_t findZipSignature (const juce::MemoryBlock& data, uint32_t signature,
                         size_t start = 0)
{
    const auto* bytes = static_cast<const uint8_t*> (data.getData());
    for (size_t i = start; i + 4 <= data.getSize(); ++i)
        if (bytes[i] == static_cast<uint8_t> (signature)
            && bytes[i + 1] == static_cast<uint8_t> (signature >> 8)
            && bytes[i + 2] == static_cast<uint8_t> (signature >> 16)
            && bytes[i + 3] == static_cast<uint8_t> (signature >> 24))
            return i;
    return std::numeric_limits<size_t>::max();
}

bool mutateZip16 (const juce::File& file, uint32_t signature, size_t fieldOffset,
                  uint16_t value)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data))
        return false;
    const auto position = findZipSignature (data, signature);
    if (position == std::numeric_limits<size_t>::max()
        || position + fieldOffset + 2 > data.getSize())
        return false;
    auto* bytes = static_cast<uint8_t*> (data.getData());
    bytes[position + fieldOffset] = static_cast<uint8_t> (value);
    bytes[position + fieldOffset + 1] = static_cast<uint8_t> (value >> 8);
    return file.replaceWithData (data.getData(), data.getSize());
}

class SkinPackageTests final : public juce::UnitTest
{
public:
    SkinPackageTests() : UnitTest ("SkinPackage") {}

    void runTest() override
    {
        beginTest ("accepts only canonical safe relative paths");
        expect (SkinPackage::isSafeRelativePath ("assets/icons/play.svg"));
        expect (! SkinPackage::isSafeRelativePath ({}));
        expect (! SkinPackage::isSafeRelativePath ("/absolute.json"));
        expect (! SkinPackage::isSafeRelativePath ("assets\\play.svg"));
        expect (! SkinPackage::isSafeRelativePath ("assets//play.svg"));
        expect (! SkinPackage::isSafeRelativePath ("assets/./play.svg"));
        expect (! SkinPackage::isSafeRelativePath ("assets/../play.svg"));
        expect (! SkinPackage::isSafeRelativePath (juce::String::repeatedString ("a", 513)));
        expect (! SkinPackage::isSafeRelativePath (juce::String::repeatedString ("a", 256) + "/x"));
        expect (! SkinPackage::isSafeRelativePath (juce::String::fromUTF8 ("assets/e\xCC\x81.txt")));
        expect (SkinPackage::isSafeRelativePath (juce::String::fromUTF8 (u8"assets/音乐 🎵 Café.txt")));
        expect (! SkinPackage::isSafeRelativePath (juce::String::fromUTF8 (u8"assets/\u1100\u1161.txt")));
        expect (SkinPackage::isSafeRelativePath (juce::String::fromUTF8 (u8"assets/가.txt")));

        beginTest ("embedded packages enforce collisions and resource budgets");
        {
            SkinValidationReport report;
            std::vector<SkinPackage::EmbeddedFile> files {
                { "assets/A.txt", juce::MemoryBlock ("a", 1) },
                { "assets/a.txt", juce::MemoryBlock ("b", 1) }
            };
            expect (! SkinPackage::fromEmbedded ("collision", std::move (files), report));
            expect (report.hasCode ("PKG005"));
        }
        {
            SkinValidationReport report;
            std::vector<SkinPackage::EmbeddedFile> files {
                { juce::String::fromUTF8 ("assets/e\xCC\x81.txt"), {} }
            };
            expect (! SkinPackage::fromEmbedded ("not-nfc", std::move (files), report));
            expect (report.hasCode ("PKG005"));
        }
        beginTest ("Unicode case folding rejects expanded and non-Latin collisions on every platform");
        for (const auto& [firstPath, secondPath] : std::initializer_list<std::pair<juce::String, juce::String>> {
                 { juce::String::fromUTF8 (u8"assets/Straße.txt"), "assets/STRASSE.txt" },
                 { juce::String::fromUTF8 (u8"assets/ς.txt"), juce::String::fromUTF8 (u8"assets/σ.txt") },
                 { juce::String::fromUTF8 (u8"assets/ﬃ.txt"), "assets/FFI.txt" } })
        {
            SkinValidationReport report;
            std::vector<SkinPackage::EmbeddedFile> files {
                { firstPath, {} }, { secondPath, {} }
            };
            expect (! SkinPackage::fromEmbedded ("unicode-fold", std::move (files), report));
            expect (report.hasCode ("PKG005"));
        }
        beginTest ("Unicode path folding preserves accents and punctuation");
        {
            SkinValidationReport report;
            std::vector<SkinPackage::EmbeddedFile> files {
                { "assets/cafe.txt", {} }, { juce::String::fromUTF8 (u8"assets/café.txt"), {} },
                { "assets/x-y.txt", {} }, { "assets/xy.txt", {} }
            };
            expect (SkinPackage::fromEmbedded ("distinct-unicode", std::move (files), report).has_value(),
                    report.toText());
        }
        beginTest ("embedded packages enforce resource budgets");
        {
            SkinValidationReport report;
            std::vector<SkinPackage::EmbeddedFile> files;
            for (int i = 0; i < 513; ++i)
                files.push_back ({ "assets/" + juce::String (i) + ".bin", {} });
            expect (! SkinPackage::fromEmbedded ("assets", std::move (files), report));
            expect (report.hasCode ("PKG009"));
        }
        {
            SkinValidationReport report;
            std::vector<SkinPackage::EmbeddedFile> files;
            for (int i = 0; i < 2049; ++i)
                files.push_back ({ "file-" + juce::String (i), {} });
            expect (! SkinPackage::fromEmbedded ("entries", std::move (files), report));
            expect (report.hasCode ("PKG004"));
        }
        {
            SkinValidationReport report;
            std::vector<SkinPackage::EmbeddedFile> files {
                { "too-large.json", juce::MemoryBlock (2 * 1024 * 1024 + 1, true) }
            };
            expect (! SkinPackage::fromEmbedded ("json", std::move (files), report));
            expect (report.hasCode ("PKG003"));
        }

        beginTest ("directory capture is private, immutable and independent of its source");
        {
            TemporaryDirectory temporary;
            const auto source = temporary.directory.getChildFile ("source");
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            source.createDirectory();
            snapshots.createDirectory();
            const auto original = writeTextFile (source, "manifest.json", "{\"name\":\"original\"}");
            writeTextFile (source, "assets/icon.txt", "captured");

            SkinValidationReport report;
            auto package = SkinPackage::captureSnapshot (source, snapshots, report);
            expect (package.has_value(), report.toText());
            if (package)
            {
                expect (! package->isArchive());
                expect (package->snapshotRoot().isAChildOf (snapshots));
                expect (package->snapshotRoot() != source);
                expect (package->sha256().length() == 64);
                original.replaceWithText ("{\"name\":\"changed\"}");
                expect (package->readText ("manifest.json", report).contains ("original"));
                expect (package->contains ("assets/icon.txt"));
                expect (package->listFiles().contains ("manifest.json"));
                report.addWarning ("RUNTIME001", {}, {}, "pre-existing warning");
                expect (package->readText ("manifest.json", report).contains ("original"));
                expect (package->verifySnapshotDigest (report));

#if JUCE_MAC || JUCE_LINUX
                struct stat snapshotInfo {};
                expectEquals (::stat (package->snapshotRoot().getFullPathName().toRawUTF8(),
                                      &snapshotInfo), 0);
                expectEquals (static_cast<int> (snapshotInfo.st_mode & 0777), 0700);
                struct stat fileInfo {};
                expectEquals (::stat (package->snapshotRoot().getChildFile ("assets/icon.txt")
                                          .getFullPathName().toRawUTF8(), &fileInfo), 0);
                expectEquals (static_cast<int> (fileInfo.st_mode & 0777), 0400);
#endif

                package->snapshotRoot().getChildFile ("manifest.json").setReadOnly (false);
                package->snapshotRoot().getChildFile ("manifest.json").replaceWithText ("tampered");
                SkinValidationReport tamperReport;
                expect (! package->verifySnapshotDigest (tamperReport));
                expect (tamperReport.hasCode ("PKG012"));
            }
        }

        beginTest ("directory capture and cleanup preserve Unicode external and package paths");
        {
            TemporaryDirectory temporary;
            const auto source = temporary.directory.getChildFile (juce::String::fromUTF8 (u8"皮肤 🎵"));
            const auto snapshots = temporary.directory.getChildFile (juce::String::fromUTF8 (u8"私有快照"));
            const auto assetPath = juce::String::fromUTF8 (u8"assets/音乐 Café.txt");
            expect (source.createDirectory().wasOk());
            expect (snapshots.createDirectory().wasOk());
            writeTextFile (source, "manifest.json", "{}");
            writeTextFile (source, assetPath, "Unicode asset");
            SkinValidationReport report;
            auto package = SkinPackage::captureSnapshot (source, snapshots, report);
            expect (package.has_value(), report.toText());
            if (package)
            {
                expectEquals (package->readText (assetPath, report), juce::String ("Unicode asset"));
                expect (package->verifySnapshotDigest (report), report.toText());
                const auto capturedRoot = package->snapshotRoot();
                package.reset();
                expect (! capturedRoot.exists());
                expect (source.getChildFile (assetPath).existsAsFile());
            }
        }

#if JUCE_MAC || JUCE_LINUX
        beginTest ("directory capture copies long non-ASCII external paths");
        {
            TemporaryDirectory temporary;
            auto externalRoot = temporary.directory;
            const auto longSegment = juce::String::fromUTF8 ("外部皮肤路径-")
                                     + juce::String::repeatedString ("long-segment-", 5);
            for (int i = 0; i < 6; ++i)
                externalRoot = externalRoot.getChildFile (longSegment + juce::String (i));

            const auto source = externalRoot.getChildFile (
                juce::String::fromUTF8 ("待安装皮肤"));
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            expect (source.createDirectory().wasOk());
            expect (snapshots.createDirectory().wasOk());
            expect (source.getFullPathName().getNumBytesAsUTF8() > 500);

            const auto contents = juce::String::fromUTF8 ("复制自非 ASCII 外部路径");
            const auto sourceFile = writeTextFile (
                source, juce::String::fromUTF8 ("assets/图标.txt"), contents);
            expect (sourceFile.existsAsFile());

            SkinValidationReport report;
            auto package = SkinPackage::captureSnapshot (source, snapshots, report);
            expect (package.has_value(), report.toText());
            if (package)
            {
                expect (package->contains (juce::String::fromUTF8 ("assets/图标.txt")));
                expectEquals (package->readText (
                                  juce::String::fromUTF8 ("assets/图标.txt"), report),
                              contents);
                expect (package->verifySnapshotDigest (report), report.toText());
            }
        }
#endif

        beginTest ("directory capture rejects links and special aliases");
#if JUCE_MAC || JUCE_LINUX
        {
            TemporaryDirectory temporary;
            const auto source = temporary.directory.getChildFile ("source");
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            source.createDirectory();
            snapshots.createDirectory();
            const auto real = writeTextFile (source, "real.json", "{}");
            real.createSymbolicLink (source.getChildFile ("linked.json"), false);
            SkinValidationReport report;
            expect (! SkinPackage::captureSnapshot (source, snapshots, report));
            expect (report.hasCode ("PKG002"));
        }
        {
            TemporaryDirectory temporary;
            const auto source = temporary.directory.getChildFile ("source");
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            source.createDirectory();
            snapshots.createDirectory();
            const auto first = writeTextFile (source, "first.json", "{}");
            const auto second = source.getChildFile ("second.json");
            expectEquals (::link (first.getFullPathName().toRawUTF8(),
                                  second.getFullPathName().toRawUTF8()), 0);
            SkinValidationReport report;
            expect (! SkinPackage::captureSnapshot (source, snapshots, report));
            expect (report.hasCode ("PKG002"));
        }
#endif

        beginTest ("directory capture rejects expanded content over 100 MiB without copying it");
        {
            TemporaryDirectory temporary;
            const auto source = temporary.directory.getChildFile ("source");
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            source.createDirectory();
            snapshots.createDirectory();
            juce::FileOutputStream sparse (source.getChildFile ("large.bin"));
            expect (sparse.setPosition (100ll * 1024ll * 1024ll));
            sparse.writeByte (0);
            sparse.flush();
            SkinValidationReport report;
            expect (! SkinPackage::captureSnapshot (source, snapshots, report));
            expect (report.hasCode ("PKG003"));
        }

        beginTest ("ZIP capture rejects traversal and no longer depends on its source");
        {
            TemporaryDirectory temporary;
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            snapshots.createDirectory();
            const auto archive = buildZip (temporary.directory.getChildFile ("valid.moonwake-skin"),
                                           { { "manifest.json", "captured zip" },
                                             { "assets/icon.txt", "icon" } });
            SkinValidationReport report;
            auto package = SkinPackage::captureSnapshot (archive, snapshots, report);
            expect (package.has_value(), report.toText());
            if (package)
            {
                expect (package->isArchive());
                archive.deleteFile();
                buildZip (archive, { { "manifest.json", "replacement" } });
                expectEquals (package->readText ("manifest.json", report),
                              juce::String ("captured zip"));
                expect (package->verifySnapshotDigest (report));
            }
        }
        {
            TemporaryDirectory temporary;
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            snapshots.createDirectory();
            const auto archive = buildZip (temporary.directory.getChildFile ("escape.moonwake-skin"),
                                           { { "../escape.json", "bad" } });
            SkinValidationReport report;
            expect (! SkinPackage::captureSnapshot (archive, snapshots, report));
            expect (report.hasCode ("PKG001"));
        }

        beginTest ("ZIP structural gates reject encryption, volumes, descriptors and links");
        {
            TemporaryDirectory temporary;
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            snapshots.createDirectory();
            const auto archive = buildZip (temporary.directory.getChildFile ("encrypted.moonwake-skin"),
                                           { { "manifest.json", "{}" } });
            expect (mutateZip16 (archive, 0x02014b50, 8, 0x0801));
            SkinValidationReport report;
            expect (! SkinPackage::captureSnapshot (archive, snapshots, report));
            expect (report.hasCode ("PKG006"));
        }
        {
            TemporaryDirectory temporary;
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            snapshots.createDirectory();
            const auto archive = buildZip (temporary.directory.getChildFile ("volume.moonwake-skin"),
                                           { { "manifest.json", "{}" } });
            expect (mutateZip16 (archive, 0x06054b50, 4, 1));
            SkinValidationReport report;
            expect (! SkinPackage::captureSnapshot (archive, snapshots, report));
            expect (report.hasCode ("PKG006"));
        }
        {
            TemporaryDirectory temporary;
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            snapshots.createDirectory();
            const auto archive = buildZip (temporary.directory.getChildFile ("descriptor.moonwake-skin"),
                                           { { "manifest.json", "{}" } });
            expect (mutateZip16 (archive, 0x04034b50, 6, 0x0808));
            expect (mutateZip16 (archive, 0x02014b50, 8, 0x0808));
            SkinValidationReport report;
            expect (! SkinPackage::captureSnapshot (archive, snapshots, report));
            expect (report.hasCode ("PKG006"));
        }
#if JUCE_MAC || JUCE_LINUX
        {
            TemporaryDirectory temporary;
            const auto snapshots = temporary.directory.getChildFile ("snapshots");
            snapshots.createDirectory();
            const auto target = writeTextFile (temporary.directory, "target.txt", "target");
            const auto link = temporary.directory.getChildFile ("link.txt");
            expect (target.createSymbolicLink (link, false));
            juce::ZipFile::Builder builder;
            builder.addFile (link, 0, "assets/link.txt");
            const auto archive = temporary.directory.getChildFile ("link.moonwake-skin");
            juce::FileOutputStream output (archive);
            expect (builder.writeToStream (output, nullptr));
            output.flush();
            SkinValidationReport report;
            expect (! SkinPackage::captureSnapshot (archive, snapshots, report));
            expect (report.hasCode ("PKG002"));
        }
#endif

        beginTest ("published archive compressed-size budget is enforced before parsing");
        {
            TemporaryDirectory temporary;
            const auto archive = temporary.directory.getChildFile ("oversized.moonwake-skin");
            juce::FileOutputStream sparse (archive);
            expect (sparse.setPosition (25ll * 1024ll * 1024ll));
            sparse.writeByte (0);
            sparse.flush();
            SkinValidationReport report;
            expect (! SkinPackage::captureSnapshot (archive,
                                                     temporary.directory.getChildFile ("snapshots"),
                                                     report));
            expect (report.hasCode ("PKG003"));
        }

        beginTest ("unsupported inputs are rejected deterministically");
        {
            TemporaryDirectory temporary;
            SkinValidationReport report;
            expect (! SkinPackage::captureSnapshot (temporary.directory.getChildFile ("missing"),
                                                     temporary.directory, report));
            expect (report.hasCode ("PKG007"));

            const auto legacyExtension = "." + juce::String ("juce") + "skin";
            const auto legacyArchive = buildZip (
                temporary.directory.getChildFile ("legacy" + legacyExtension),
                { { "manifest.json", "{}" } });
            SkinValidationReport legacyReport;
            expect (! SkinPackage::captureSnapshot (
                legacyArchive, temporary.directory.getChildFile ("legacy-snapshots"),
                legacyReport));
            expect (legacyReport.hasCode ("PKG007"));
        }
    }
};

SkinPackageTests skinPackageTests;
} // namespace
