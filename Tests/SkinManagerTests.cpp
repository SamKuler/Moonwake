#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "skin/SkinInstaller.h"
#include "skin/SkinManager.h"
#include "skin/SkinRegistry.h"
#include "skin/SkinSession.h"

#include <juce_core/juce_core.h>

#include <barrier>
#include <thread>

namespace
{
juce::File freshDirectory (const juce::String& name)
{
    const auto root = juce::File::getCurrentWorkingDirectory()
                          .getChildFile ("build")
                          .getChildFile ("skin-manager-tests")
                          .getChildFile (name + "-" + juce::Uuid().toString());
    static_cast<void> (root.deleteRecursively());
    root.createDirectory();
    return root;
}

juce::File writeFixture (const juce::File& root, const juce::String& id,
                         const juce::String& version, juce::Colour accent)
{
    const auto source = root.getChildFile (id + "-" + version);
    source.createDirectory();
    const auto manifest = juce::String (R"json({
      "skinApi": 1,
      "id": ")json") + id + R"json(",
      "name": "Manager Fixture",
      "version": ")json" + version + R"json(",
      "author": { "name": "Tests" },
      "minAppVersion": "0.1.0",
      "extends": "builtin.foundation",
      "capabilities": ["tokens"],
      "entrypoints": { "tokens": "tokens.json" }
    })json";
    source.getChildFile ("manifest.json").replaceWithText (manifest);
    source.getChildFile ("tokens.json").replaceWithText (
        "{\"colors\":{\"managerAccent\":\"#"
        + accent.toDisplayString (false) + "\"}}");
    return source;
}

std::shared_ptr<const CompiledSkin> foundationSkin (SkinValidationReport& report)
{
    const auto package = SkinBuiltInPackages::create (
        SkinBuiltInPackages::Package::foundation, report);
    return package ? SkinCompiler::compile (*package, {}, report) : nullptr;
}

std::optional<SkinPackage> capture (const juce::File& source,
                                    const juce::File& snapshots,
                                    SkinValidationReport& report)
{
    return SkinPackage::captureSnapshot (source, snapshots, report);
}

int countSelection (const std::vector<SkinRegistryEntry>& entries,
                    const juce::String& id)
{
    return static_cast<int> (std::count_if (
        entries.begin(), entries.end(), [&] (const auto& entry)
        {
            return entry.selection.id == id;
        }));
}

class SkinManagerTests final : public juce::UnitTest
{
public:
    SkinManagerTests() : UnitTest ("SkinManager", "skin") {}

    void runTest() override
    {
        beginTest ("registry exposes built-ins and atomically installed versions");
        {
            const auto root = freshDirectory ("registry");
            const auto sources = root.getChildFile ("sources");
            const auto snapshots = root.getChildFile ("snapshots");
            const auto installs = root.getChildFile ("installs");
            sources.createDirectory();
            snapshots.createDirectory();

            SkinValidationReport report;
            const auto foundation = foundationSkin (report);
            expect (foundation != nullptr, report.toText());
            SkinInstaller installer (installs);
            for (const auto& fixture : {
                     std::pair<juce::String, juce::Colour> { "1.0.0", juce::Colours::red },
                     { "1.1.0", juce::Colours::green },
                     { "2.0.0", juce::Colours::blue } })
            {
                const auto source = writeFixture (
                    sources, "com.example.manager", fixture.first, fixture.second);
                auto package = capture (source, snapshots, report);
                auto compiled = package && foundation
                    ? SkinCompiler::compile (*package, foundation, report) : nullptr;
                expect (compiled != nullptr, report.toText());
                if (package && compiled)
                    expect (installer.install (*package, compiled->manifest, {}, report),
                            report.toText());
                if (fixture.first == "1.0.0" && package && compiled)
                    expect (installer.install (*package, compiled->manifest, {}, report),
                            report.toText());
            }

            expect (! installs.getChildFile ("com.example.manager")
                         .getChildFile ("1.0.0").exists());
            expect (installs.getChildFile ("com.example.manager")
                        .getChildFile ("1.1.0").isDirectory());
            expect (installs.getChildFile ("com.example.manager")
                        .getChildFile ("2.0.0").isDirectory());
            expect (installs.findChildFiles (
                        juce::File::findDirectories, false, ".installing-*").isEmpty());
            expect (installs.getChildFile ("com.example.manager").findChildFiles (
                        juce::File::findFiles, false, ".pruning-*").isEmpty());
            expect (installs.getChildFile ("com.example.manager").findChildFiles (
                        juce::File::findDirectories, false, ".removing-*").isEmpty());

            SkinRegistry registry;
            expect (registry.initialise (installs, report), report.toText());
            const auto entries = registry.entries();
            expectEquals (countSelection (entries, "builtin.foundation"), 1);
            expectEquals (countSelection (entries, "builtin.default"), 1);
            expectEquals (countSelection (entries, "com.example.manager"), 2);
            expect (registry.contains ({ "com.example.manager", "2.0.0" }));
            const auto metadata = std::find_if (
                entries.begin(), entries.end(), [] (const auto& entry)
                {
                    return entry.selection
                        == SkinSelection { "com.example.manager", "2.0.0" };
                });
            expect (metadata != entries.end());
            if (metadata != entries.end())
            {
                expectEquals (metadata->name, juce::String ("Manager Fixture"));
                expectEquals (metadata->author, juce::String ("Tests"));
            }

            auto installedPackage = registry.packageFor (
                { "com.example.manager", "2.0.0" }, snapshots, report);
            auto installedSkin = installedPackage
                ? SkinCompiler::compile (*installedPackage, foundation, report) : nullptr;
            expect (installedSkin != nullptr, report.toText());
            if (installedSkin)
                expectEquals (installedSkin->manifest.version, juce::String ("2.0.0"));

            expect (installs.getChildFile ("com.example.manager")
                        .getChildFile ("2.0.0")
                        .getChildFile ("tokens.json")
                        .replaceWithText ("{\"colors\":{\"tampered\":\"#FFFFFFFF\"}}"));
            SkinValidationReport tamperReport;
            expect (! registry.packageFor (
                { "com.example.manager", "2.0.0" }, snapshots, tamperReport));
            expect (tamperReport.hasCode ("RUNTIME004"), tamperReport.toText());

            expect (! installer.remove ({ "builtin.default", "1.0.0" }, {}, report));
            expect (installer.remove ({ "com.example.manager", "1.1.0" }, {}, report),
                    report.toText());
            expect (! installs.getChildFile ("com.example.manager")
                         .getChildFile ("1.1.0").exists());

            SkinValidationReport unsafeReport;
            expect (! installer.remove ({ "../escape", "1.0.0" }, {}, unsafeReport));
            expect (! root.getChildFile ("escape").exists());
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("concurrent duplicate installs preserve one complete registry entry");
        {
            const auto root = freshDirectory ("concurrent-install");
            const auto source = writeFixture (
                root, "com.example.concurrent", "1.0.0", juce::Colours::yellow);
            const auto snapshots = root.getChildFile ("snapshots");
            const auto installs = root.getChildFile ("installs");
            SkinValidationReport compileReport;
            const auto foundation = foundationSkin (compileReport);
            auto package = capture (source, snapshots, compileReport);
            auto compiled = package && foundation
                ? SkinCompiler::compile (*package, foundation, compileReport) : nullptr;
            expect (compiled != nullptr, compileReport.toText());
            if (package && compiled)
            {
                SkinInstaller first (installs);
                SkinInstaller second (installs);
                SkinValidationReport firstReport;
                SkinValidationReport secondReport;
                bool firstResult = false;
                bool secondResult = false;
                std::barrier startBarrier (2);
                std::thread firstThread ([&]
                {
                    startBarrier.arrive_and_wait();
                    firstResult = first.install (*package, compiled->manifest, {},
                                                 firstReport);
                });
                std::thread secondThread ([&]
                {
                    startBarrier.arrive_and_wait();
                    secondResult = second.install (*package, compiled->manifest, {},
                                                   secondReport);
                });
                firstThread.join();
                secondThread.join();
                expect (firstResult, firstReport.toText());
                expect (secondResult, secondReport.toText());

                SkinRegistry registry;
                SkinValidationReport registryReport;
                expect (registry.initialise (installs, registryReport),
                        registryReport.toText());
                expect (registry.contains ({ "com.example.concurrent", "1.0.0" }));
                expect (registry.packageFor ({ "com.example.concurrent", "1.0.0" },
                                             snapshots, registryReport).has_value(),
                        registryReport.toText());
            }
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("cross-process duplicate installs share first-time lock creation safely");
        {
            const auto root = freshDirectory ("cross-process-install");
            const auto source = writeFixture (
                root, "com.example.crossprocess", "1.0.0", juce::Colours::cyan);
            const auto snapshots = root.getChildFile ("snapshots");
            const auto installs = root.getChildFile ("installs");
            const auto barrier = root.getChildFile ("barrier");
            expect (snapshots.getChildFile ("first").createDirectory());
            expect (snapshots.getChildFile ("second").createDirectory());
            expect (barrier.createDirectory());

            const auto executable = juce::File::getSpecialLocation (
                juce::File::currentExecutableFile).getFullPathName();
            const auto arguments = [&] (const juce::String& workerId)
            {
                return juce::StringArray {
                    executable,
                    "--skin-install-worker",
                    source.getFullPathName(),
                    snapshots.getFullPathName(),
                    installs.getFullPathName(),
                    barrier.getFullPathName(),
                    workerId
                };
            };

            juce::ChildProcess first;
            juce::ChildProcess second;
            expect (first.start (arguments ("first")));
            expect (second.start (arguments ("second")));

            const auto readyDeadline = juce::Time::getMillisecondCounterHiRes()
                                     + 10000.0;
            while ((! barrier.getChildFile ("first.ready").existsAsFile()
                    || ! barrier.getChildFile ("second.ready").existsAsFile())
                   && juce::Time::getMillisecondCounterHiRes() < readyDeadline)
                juce::Thread::sleep (1);
            expect (barrier.getChildFile ("first.ready").existsAsFile());
            expect (barrier.getChildFile ("second.ready").existsAsFile());
            expect (barrier.getChildFile ("start").replaceWithText ("start"));

            expect (first.waitForProcessToFinish (10000));
            expect (second.waitForProcessToFinish (10000));
            expect (first.getExitCode() == 0u,
                    "first worker exit "
                        + juce::String (static_cast<int> (first.getExitCode()))
                        + ": " + first.readAllProcessOutput());
            expect (second.getExitCode() == 0u,
                    "second worker exit "
                        + juce::String (static_cast<int> (second.getExitCode()))
                        + ": " + second.readAllProcessOutput());

            SkinRegistry registry;
            SkinValidationReport registryReport;
            expect (registry.initialise (installs, registryReport),
                    registryReport.toText());
            expect (registry.contains ({ "com.example.crossprocess", "1.0.0" }));
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("startup recovery restores a prune transaction interrupted before commit");
        {
            const auto root = freshDirectory ("prune-recovery");
            const auto source = writeFixture (
                root, "com.example.recovery", "1.0.0", juce::Colours::orange);
            const auto snapshots = root.getChildFile ("snapshots");
            const auto installs = root.getChildFile ("installs");
            SkinValidationReport report;
            const auto foundation = foundationSkin (report);
            auto package = capture (source, snapshots, report);
            auto compiled = package && foundation
                ? SkinCompiler::compile (*package, foundation, report) : nullptr;
            SkinInstaller installer (installs);
            expect (package && compiled
                    && installer.install (*package, compiled->manifest, {}, report),
                    report.toText());

            const auto idRoot = installs.getChildFile ("com.example.recovery");
            const auto transaction = juce::Uuid().toString();
            const auto staged = idRoot.getChildFile (
                ".removing-" + transaction + "-1.0.0");
            const auto digest = idRoot.getChildFile (".digests")
                                      .getChildFile ("1.0.0.sha256");
            const auto stagedDigest = idRoot.getChildFile (".digests")
                .getChildFile (staged.getFileName() + ".sha256");
            expect (idRoot.getChildFile (".pruning-" + transaction)
                        .replaceWithText ("2.0.0\n"));
            expect (idRoot.getChildFile ("1.0.0").moveFileTo (staged));
            expect (digest.moveFileTo (stagedDigest));
            expect (installer.recover (report), report.toText());
            expect (idRoot.getChildFile ("1.0.0").isDirectory());
            expect (digest.existsAsFile());
            expect (! staged.exists());
            expect (! idRoot.getChildFile (".pruning-" + transaction).exists());
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("failed installation cleans its sibling temporary directory");
        {
            const auto root = freshDirectory ("failed-install");
            const auto source = writeFixture (
                root, "com.example.failure", "1.0.0", juce::Colours::orange);
            const auto snapshots = root.getChildFile ("snapshots");
            const auto installs = root.getChildFile ("installs");
            SkinValidationReport report;
            const auto foundation = foundationSkin (report);
            auto package = capture (source, snapshots, report);
            auto compiled = package && foundation
                ? SkinCompiler::compile (*package, foundation, report) : nullptr;
            SkinInstaller failingInstaller (
                installs, [] (const juce::File&, const juce::MemoryBlock&) { return false; });
            expect (compiled != nullptr, report.toText());
            if (package && compiled)
            {
                auto unsafeManifest = compiled->manifest;
                unsafeManifest.id = "../escape";
                SkinValidationReport unsafeReport;
                SkinInstaller normalInstaller (installs);
                expect (! normalInstaller.install (
                    *package, unsafeManifest, {}, unsafeReport));
                expect (! root.getChildFile ("escape").exists());
                expect (! failingInstaller.install (
                    *package, compiled->manifest, {}, report));
            }
            const auto idRoot = installs.getChildFile ("com.example.failure");
            expect (idRoot.findChildFiles (
                        juce::File::findDirectories, false, ".installing-*").isEmpty());
            expect (! idRoot.getChildFile ("1.0.0").exists());
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("default install root is the platform's application-data skin directory");
        {
           #if JUCE_MAC
            const auto root = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                .getChildFile ("Library/Application Support/Moonwake");
           #else
            const auto root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                .getChildFile ("Moonwake");
           #endif
            expect (SkinRegistry::defaultInstallRoot() == root.getChildFile ("Skins"));
        }

        beginTest ("session records each runtime diagnostic once and escalates health");
        {
            SkinValidationReport report;
            const auto foundation = foundationSkin (report);
            expect (foundation != nullptr, report.toText());
            if (! foundation) return;

            int fatalCallbacks = 0;
            SkinSession session (foundation, false);
            session.setFatalCallback ([&] { ++fatalCallbacks; });
            SkinDiagnostic recoverable {
                SkinDiagnosticSeverity::warning, "RUNTIME001", "visual", "/layer",
                "Optional visual failed.", "Skip it."
            };
            session.reportRenderFault (recoverable, SkinRenderStatus::recoverableFault);
            session.reportRenderFault (recoverable, SkinRenderStatus::recoverableFault);
            expect (session.health() == SkinSessionHealth::degraded);
            expectEquals (static_cast<int> (
                              session.runtimeReport().getDiagnostics().size()), 1);

            SkinDiagnostic fatal {
                SkinDiagnosticSeverity::error, "RUNTIME002", "textButton", "/body",
                "Required renderer failed.", "Restore Foundation."
            };
            session.reportRenderFault (fatal, SkinRenderStatus::fatalFault);
            session.reportRenderFault (fatal, SkinRenderStatus::fatalFault);
            expect (session.health() == SkinSessionHealth::fatal);
            expectEquals (static_cast<int> (
                              session.runtimeReport().getDiagnostics().size()), 2);
            expectEquals (fatalCallbacks, 1);
        }

        beginTest ("preview apply cancel persistence and fatal fallback are atomic");
        {
            class Listener final : public SkinManager::Listener
            {
            public:
                void skinChanged() override { ++calls; }
                int calls = 0;
            } listener;

            const auto root = freshDirectory ("lifecycle");
            const auto source = writeFixture (
                root, "com.example.lifecycle", "1.0.0", juce::Colours::purple);
            SkinManager manager;
            SkinValidationReport report;
            expect (manager.initialise (root.getChildFile ("installs"), report),
                    report.toText());
            expect (manager.activeSelection()
                    == SkinSelection { "builtin.default", "1.0.0" });
            const auto originalSession = manager.activeSession();
            expect (originalSession != nullptr && ! originalSession->isPreview());

            auto importReport = manager.importSkin (source);
            expect (! importReport.hasErrors(), importReport.toText());
            const auto installed = manager.installedSkins();
            expect (installed.end()
                    != std::find (installed.begin(), installed.end(),
                                  SkinSelection { "com.example.lifecycle", "1.0.0" }));

            int persisted = 0;
            SkinSelection persistedSelection;
            manager.setPersistenceCallback ([&] (const SkinSelection& selection)
            {
                ++persisted;
                persistedSelection = selection;
            });
            manager.addListener (&listener);

            expect (manager.preview (
                { "com.example.lifecycle", "1.0.0" }, report), report.toText());
            expect (manager.isPreviewing());
            expect (manager.activeSession()->isPreview());
            expect (manager.committedSelection()
                    == SkinSelection { "builtin.default", "1.0.0" });
            expectEquals (persisted, 0);
            const auto afterPreviewNotifications = listener.calls;
            manager.cancelPreview();
            expect (! manager.isPreviewing());
            expect (manager.activeSession() == originalSession);
            expectEquals (listener.calls, afterPreviewNotifications + 1);

            expect (manager.preview (
                { "com.example.lifecycle", "1.0.0" }, report), report.toText());
            expect (manager.applyPreview (report), report.toText());
            expect (! manager.isPreviewing());
            expect (! manager.activeSession()->isPreview());
            expect (manager.committedSelection()
                    == SkinSelection { "com.example.lifecycle", "1.0.0" });
            expectEquals (persisted, 1);
            expect (persistedSelection == manager.committedSelection());
            SkinValidationReport removeActiveReport;
            expect (! manager.remove (
                { "com.example.lifecycle", "1.0.0" }, removeActiveReport));
            expect (manager.activeSession() != nullptr);

            const auto beforeFailedPreview = manager.activeSession();
            SkinValidationReport missingReport;
            expect (! manager.preview (
                { "com.example.missing", "1.0.0" }, missingReport));
            expect (manager.activeSession() == beforeFailedPreview);
            expectEquals (persisted, 1);

            SkinDiagnostic fatal {
                SkinDiagnosticSeverity::error, "RUNTIME002", "textButton", "/body",
                "Simulated required renderer failure.", "Restore Foundation."
            };
            beforeFailedPreview->reportRenderFault (
                fatal, SkinRenderStatus::fatalFault);
            expect (manager.activeSession() == beforeFailedPreview);
            manager.processPendingFallback();
            expect (manager.activeSelection()
                    == SkinSelection { "builtin.foundation", "1.0.0" });
            expect (manager.activeSession()->health() == SkinSessionHealth::healthy);
            const auto failed = manager.lastFailedSession();
            expect (failed.has_value());
            if (failed)
            {
                expect (failed->selection
                        == SkinSelection { "com.example.lifecycle", "1.0.0" });
                expect (failed->health == SkinSessionHealth::fatal);
                expectEquals (static_cast<int> (
                                  failed->runtimeReport.getDiagnostics().size()), 1);
                expect (failed->runtimeReport.hasCode ("RUNTIME002"));
            }
            expectEquals (persisted, 2);
            manager.clearLastFailedSession();
            expect (! manager.lastFailedSession().has_value());
            manager.removeListener (&listener);
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("developer reload previews a fresh immutable snapshot");
        {
            const auto root = freshDirectory ("developer");
            const auto source = writeFixture (
                root, "com.example.developer", "1.0.0", juce::Colours::cyan);
            SkinManager manager;
            SkinValidationReport report;
            expect (manager.initialise (root.getChildFile ("installs"), report),
                    report.toText());
            expect (manager.setDeveloperDirectory (source, report), report.toText());
            expectEquals (manager.developerDirectory().getFullPathName(),
                          source.getFullPathName());
            expect (manager.reloadDeveloperSkin (report), report.toText());
            expect (manager.isPreviewing());
            expect (manager.activeSelection()
                    == SkinSelection { "com.example.developer", "1.0.0" });
            SkinValidationReport applyDeveloperReport;
            expect (! manager.applyPreview (applyDeveloperReport));
            expect (manager.isPreviewing());
            manager.cancelPreview();
            expect (manager.activeSelection()
                    == SkinSelection { "builtin.default", "1.0.0" });
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("restoring a developer directory preserves committed skin state");
        {
            const auto root = freshDirectory ("developer-restore");
            const auto source = writeFixture (
                root, "com.example.developer-restore", "1.0.0",
                juce::Colours::orange);
            SkinManager manager;
            SkinValidationReport report;
            expect (manager.initialise (root.getChildFile ("installs"), report),
                    report.toText());
            const auto committed = manager.committedSelection();
            const auto session = manager.activeSession();

            expect (manager.restoreDeveloperDirectory (source, report),
                    report.toText());
            expectEquals (manager.developerDirectory().getFullPathName(),
                          source.getFullPathName());
            expect (! manager.isPreviewing());
            expect (manager.committedSelection() == committed);
            expect (manager.activeSession() == session);
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("preview protects committed and previewed versions from pruning");
        {
            const auto root = freshDirectory ("preview-protection");
            const auto first = writeFixture (
                root, "com.example.protected", "1.0.0", juce::Colours::red);
            const auto second = writeFixture (
                root, "com.example.protected", "2.0.0", juce::Colours::green);
            const auto third = writeFixture (
                root, "com.example.protected", "3.0.0", juce::Colours::blue);
            SkinManager manager;
            SkinValidationReport report;
            expect (manager.initialise (root.getChildFile ("installs"), report),
                    report.toText());
            expect (! manager.importSkin (first).hasErrors());
            expect (! manager.importSkin (second).hasErrors());
            expect (manager.activate (
                { "com.example.protected", "1.0.0" }, false, report), report.toText());
            expect (manager.preview (
                { "com.example.protected", "2.0.0" }, report), report.toText());

            SkinValidationReport removeReport;
            expect (! manager.remove (
                { "com.example.protected", "1.0.0" }, removeReport));
            const auto conflictReport = manager.importSkin (third);
            expect (conflictReport.hasErrors());
            const auto installed = manager.installedSkins();
            expect (std::find (installed.begin(), installed.end(),
                              SkinSelection { "com.example.protected", "1.0.0" })
                    != installed.end());
            expect (std::find (installed.begin(), installed.end(),
                              SkinSelection { "com.example.protected", "2.0.0" })
                    != installed.end());
            expect (std::find (installed.begin(), installed.end(),
                              SkinSelection { "com.example.protected", "3.0.0" })
                    == installed.end());
            manager.cancelPreview();
            expect (manager.activeSelection()
                    == SkinSelection { "com.example.protected", "1.0.0" });
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("fatal evidence survives a session switch before async fallback");
        {
            const auto root = freshDirectory ("fatal-race");
            const auto source = writeFixture (
                root, "com.example.fatal", "1.0.0", juce::Colours::pink);
            SkinManager manager;
            SkinValidationReport report;
            expect (manager.initialise (root.getChildFile ("installs"), report),
                    report.toText());
            expect (! manager.importSkin (source).hasErrors());
            expect (manager.activate (
                { "com.example.fatal", "1.0.0" }, false, report), report.toText());
            const auto failingSession = manager.activeSession();
            SkinDiagnostic fatal {
                SkinDiagnosticSeverity::error, "RUNTIME002", "textButton", "/body",
                "Fatal before preview switch.", "Restore Foundation."
            };
            failingSession->reportRenderFault (fatal, SkinRenderStatus::fatalFault);
            expect (manager.preview ({ "builtin.default", "1.0.0" }, report),
                    report.toText());
            manager.processPendingFallback();
            expect (manager.activeSelection()
                    == SkinSelection { "builtin.default", "1.0.0" });
            expect (manager.committedSelection()
                    == SkinSelection { "builtin.foundation", "1.0.0" });
            const auto failed = manager.lastFailedSession();
            expect (failed.has_value()
                    && failed->selection
                        == SkinSelection { "com.example.fatal", "1.0.0" });
            manager.cancelPreview();
            expect (manager.activeSelection()
                    == SkinSelection { "builtin.foundation", "1.0.0" });
            static_cast<void> (root.deleteRecursively());
        }
    }
};

SkinManagerTests skinManagerTests;
} // namespace
