#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "plugins/PluginManager.h"
#include "plugins/PluginScanProcess.h"
#include "plugins/PluginFilter.h"

namespace
{
bool waitForScanToFinish (PluginManager& manager, int timeoutMilliseconds)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes()
                        + timeoutMilliseconds;
    while (manager.getScanStatus().scanning
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep (5);
    return ! manager.getScanStatus().scanning;
}

bool waitForFile (const juce::File& file, int timeoutMilliseconds = 3000)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes()
                        + timeoutMilliseconds;
    while (! file.existsAsFile()
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep (1);
    return file.existsAsFile();
}

juce::File freshPluginTestDirectory (juce::StringRef name)
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
        .getChildFile ("build")
        .getChildFile (name);
    static_cast<void> (directory.deleteRecursively());
    static_cast<void> (directory.createDirectory());
    return directory;
}

PluginScannerConfiguration fixtureConfiguration (juce::String mode,
                                                 int timeoutMilliseconds)
{
    PluginScannerConfiguration configuration;
    configuration.executable = juce::File::getSpecialLocation (
        juce::File::currentExecutableFile);
    configuration.resultDirectory = freshPluginTestDirectory (
        "plugin-scanner-results");
    configuration.argumentsBeforeProtocol = {
        "--plugin-scan-fixture", std::move (mode)
    };
    configuration.perCandidateTimeoutMilliseconds = timeoutMilliseconds;
    configuration.candidateProvider = [] (const juce::String& formatName,
                                          const juce::FileSearchPath&)
    {
        return formatName == "VST3"
                 ? juce::StringArray { "/tmp/MoonwakeFixture.vst3" }
                 : juce::StringArray {};
    };
    return configuration;
}

class PluginManagerTests final : public juce::UnitTest
{
public:
    PluginManagerTests()
        : UnitTest ("PluginManager", "plugins")
    {
    }

    void runTest() override
    {
        beginTest ("plug-in facets combine reported type, manufacturer, format and search terms");
        {
            juce::PluginDescription plugin;
            plugin.name = "Clean Equalizer";
            plugin.manufacturerName = " Acme Audio ";
            plugin.pluginFormatName = "VST3";
            plugin.category = "Fx|EQ";
            PluginFilter filter { "clean acme", "EQ", "acme audio", "VST3" };
            expect (filter.matches (plugin));
            filter.search = "Fx acme"; expect (filter.matches (plugin));
            filter.search = "clean acme";
            filter.type = "Dynamics"; expect (! filter.matches (plugin));
            filter.type = "Effect"; expect (filter.matches (plugin));
            filter.manufacturer = "Other"; expect (! filter.matches (plugin));
            filter.manufacturer.clear(); filter.format = "AudioUnit"; expect (! filter.matches (plugin));
            filter.format.clear(); filter.search = "missing"; expect (! filter.matches (plugin));
            plugin.pluginFormatName = "AudioUnit"; filter.search = "AU clean"; expect (filter.matches (plugin));
            plugin.category.clear(); plugin.manufacturerName.clear();
            filter = { {}, "Uncategorised", "Unknown manufacturer", {} };
            expect (filter.matches (plugin));
            plugin.isInstrument = true;
            expect (PluginFilter::types (plugin).contains ("Instrument"));
        }

        beginTest ("disabled formats never enter scanner discovery and all-off reports a useful action");
        {
            auto configuration = fixtureConfiguration ("success", 2000);
            juce::StringArray discoveredFormats;
            configuration.candidateProvider = [&] (const juce::String& name, const juce::FileSearchPath&)
            {
                discoveredFormats.add (name);
                return juce::StringArray {};
            };
            PluginManager manager (configuration);
            expect (manager.setFormatPreferences ({ false, true }));
            expect (manager.startScan());
            expect (waitForScanToFinish (manager, 3000));
            expect (manager.cancelAndWaitForScan());
            expect (! discoveredFormats.contains ("VST3"));
           #if JUCE_MAC && JUCE_PLUGINHOST_AU
            expect (discoveredFormats.contains ("AudioUnit"));
           #endif
            expect (manager.setFormatPreferences ({ true, false }));
            discoveredFormats.clear();
            expect (manager.startScan());
            expect (waitForScanToFinish (manager, 3000));
            expect (manager.cancelAndWaitForScan());
            expect (discoveredFormats == juce::StringArray { "VST3" });
            expect (manager.setFormatPreferences ({ false, false }));
            expect (! manager.startScan());
            expect (manager.getScanStatus().message.contains ("Enable"));
            expect (! manager.startScan (configuration.resultDirectory));
            expect (manager.getScanStatus().message.contains ("VST3"));
        }
        beginTest ("registers VST3 and AudioUnit formats on macOS");
        {
            PluginManager manager;

            expect (manager.getRegisteredFormatNames().contains ("VST3"));
           #if JUCE_MAC
            expect (manager.getRegisteredFormatNames().contains ("AudioUnit"));
           #endif
        }

        beginTest ("scanner child process classifies completion and failure");
        {
            const auto executable = juce::File::getSpecialLocation (
                juce::File::currentExecutableFile);
            const auto success = PluginScanProcess::run (
                { executable.getFullPathName(), "--plugin-scan-probe", "success" },
                1000, [] { return false; });
            expect (success.outcome == PluginScanProcessOutcome::completed,
                    success.diagnostic);
            expectEquals (static_cast<int> (success.exitCode), 0);

            const auto failure = PluginScanProcess::run (
                { executable.getFullPathName(), "--plugin-scan-probe", "failure" },
                1000, [] { return false; });
            expect (failure.outcome == PluginScanProcessOutcome::failed,
                    failure.diagnostic);
            expectEquals (static_cast<int> (failure.exitCode), 73);
        }

        beginTest ("scanner child process enforces timeout and cancellation");
        {
            const auto executable = juce::File::getSpecialLocation (
                juce::File::currentExecutableFile);
            const auto started = juce::Time::getMillisecondCounterHiRes();
            const auto timedOut = PluginScanProcess::run (
                { executable.getFullPathName(), "--plugin-scan-probe", "hang" },
                120, [] { return false; });
            expect (timedOut.outcome == PluginScanProcessOutcome::timedOut,
                    timedOut.diagnostic);
            expect (juce::Time::getMillisecondCounterHiRes() - started < 1500.0);

            const auto cancellationStart = juce::Time::getMillisecondCounterHiRes();
            const auto cancelled = PluginScanProcess::run (
                { executable.getFullPathName(), "--plugin-scan-probe", "hang" },
                5000, [cancellationStart]
                {
                    return juce::Time::getMillisecondCounterHiRes()
                               - cancellationStart >= 80.0;
                });
            expect (cancelled.outcome == PluginScanProcessOutcome::cancelled,
                    cancelled.diagnostic);
            expect (juce::Time::getMillisecondCounterHiRes()
                        - cancellationStart < 1500.0);
        }

        beginTest ("manager accepts only successful isolated scanner results");
        {
            const auto root = freshPluginTestDirectory (
                "moonwake-plugin-success");
            expect (root.isDirectory());
            PluginManager manager (fixtureConfiguration ("success", 1000));
            expect (manager.startScan (root));
            expect (waitForScanToFinish (manager, 3000));
            const auto status = manager.getScanStatus();
            expectEquals (manager.getKnownPlugins().size(), 1);
            expectEquals (status.discovered, 1);
            expectEquals (status.failed, 0);
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("concurrent managers never share scanner result files");
        {
            const auto root = freshPluginTestDirectory (
                "moonwake-plugin-concurrent");
            const auto resultDirectory = root.getChildFile ("results");
            const auto barrier = root.getChildFile ("barrier");
            expect (resultDirectory.createDirectory());
            expect (barrier.createDirectory());

            auto firstConfiguration = fixtureConfiguration (
                "collision-first", 6000);
            firstConfiguration.resultDirectory = resultDirectory;
            firstConfiguration.argumentsBeforeProtocol = {
                "--plugin-scan-fixture", "collision-first",
                barrier.getFullPathName()
            };
            firstConfiguration.candidateProvider = [] (
                const juce::String& formatName, const juce::FileSearchPath&)
            {
                return formatName == "VST3"
                         ? juce::StringArray { "/tmp/ConcurrentFirst.vst3" }
                         : juce::StringArray {};
            };

            auto secondConfiguration = fixtureConfiguration (
                "collision-second", 6000);
            secondConfiguration.resultDirectory = resultDirectory;
            secondConfiguration.argumentsBeforeProtocol = {
                "--plugin-scan-fixture", "collision-second",
                barrier.getFullPathName()
            };
            secondConfiguration.candidateProvider = [] (
                const juce::String& formatName, const juce::FileSearchPath&)
            {
                return formatName == "VST3"
                         ? juce::StringArray { "/tmp/ConcurrentSecond.vst3" }
                         : juce::StringArray {};
            };

            PluginManager first (std::move (firstConfiguration));
            PluginManager second (std::move (secondConfiguration));
            expect (first.startScan (root));
            expect (waitForFile (barrier.getChildFile ("first.ready")));
            expect (second.startScan (root));
            expect (waitForScanToFinish (first, 8000));
            expect (waitForScanToFinish (second, 8000));

            const auto firstPlugins = first.getKnownPlugins();
            const auto secondPlugins = second.getKnownPlugins();
            expectEquals (firstPlugins.size(), 1);
            expectEquals (secondPlugins.size(), 1);
            if (! firstPlugins.isEmpty())
                expectEquals (firstPlugins[0].fileOrIdentifier,
                              juce::String ("/tmp/ConcurrentFirst.vst3"));
            if (! secondPlugins.isEmpty())
                expectEquals (secondPlugins[0].fileOrIdentifier,
                              juce::String ("/tmp/ConcurrentSecond.vst3"));
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("manager survives scanner failure and timeout");
        {
            const auto root = freshPluginTestDirectory (
                "moonwake-plugin-failure");
            expect (root.isDirectory());

            PluginManager failed (fixtureConfiguration ("failure", 1000));
            expect (failed.startScan (root));
            expect (waitForScanToFinish (failed, 3000));
            expectEquals (failed.getKnownPlugins().size(), 0);
            expectEquals (failed.getScanStatus().failed, 1);
            expect (failed.getScanStatus().message.containsIgnoreCase ("exited"));

            PluginManager timedOut (fixtureConfiguration ("hang", 100));
            expect (timedOut.startScan (root));
            expect (waitForScanToFinish (timedOut, 3000));
            expectEquals (timedOut.getKnownPlugins().size(), 0);
            expectEquals (timedOut.getScanStatus().failed, 1);
            expect (timedOut.getScanStatus().message.containsIgnoreCase ("time"));
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("manager cancellation terminates an isolated scanner before cache save");
        {
            const auto root = freshPluginTestDirectory (
                "moonwake-plugin-cancel");
            expect (root.isDirectory());
            PluginManager manager (fixtureConfiguration ("hang", 5000));
            expect (manager.startScan (root));
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
            while (manager.getScanStatus().currentItem.isEmpty()
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::Thread::sleep (5);
            expect (! manager.setFormatPreferences ({ false, false }));
            expect (manager.getFormatPreferences() == PluginFormatPreferences {});
            const auto cancelStarted = juce::Time::getMillisecondCounterHiRes();
            expect (manager.cancelAndWaitForScan (1500));
            expect (juce::Time::getMillisecondCounterHiRes()
                        - cancelStarted < 1400.0);
            expect (manager.getScanStatus().message.containsIgnoreCase ("cancel"));

            juce::String error;
            expect (manager.writeKnownPluginCacheAtomically (
                        root.getChildFile ("plugins.xml"), error), error);
            static_cast<void> (root.deleteRecursively());
        }

        beginTest ("manager rejects a missing scanner helper without starting");
        {
            auto configuration = fixtureConfiguration ("success", 1000);
            configuration.executable = juce::File (
                "/path/that/does/not/exist/MoonwakePluginScanner");
            PluginManager manager (std::move (configuration));
            expect (! manager.startScan());
            expect (! manager.getScanStatus().scanning);
            expect (manager.getScanStatus().message.containsIgnoreCase ("not found"));
        }

        beginTest ("filters instruments out of the insert effect list");
        {
            juce::Array<juce::PluginDescription> plugins;

            juce::PluginDescription eq;
            eq.name = "Clean EQ";
            eq.manufacturerName = "Tools";
            eq.category = "Fx|EQ";
            eq.isInstrument = false;
            plugins.add (eq);

            juce::PluginDescription synth;
            synth.name = "Big Synth";
            synth.manufacturerName = "Keys";
            synth.category = "Instrument|Synth";
            synth.isInstrument = true;
            plugins.add (synth);

            const auto effects = PluginManager::filterInsertEffects (plugins);

            expectEquals (effects.size(), 1);
            expectEquals (effects[0].name, juce::String ("Clean EQ"));
            expectEquals (PluginManager::getDisplayGroup (effects[0]), juce::String ("Fx / EQ - Tools"));
        }

        beginTest ("known plug-in cache is written and read atomically");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile ("build")
                                       .getChildFile ("plugin-manager-tests");
            expect (directory.createDirectory());
            const auto cacheFile = directory.getChildFile ("plugins.xml");
            static_cast<void> (cacheFile.deleteFile());

            PluginManager writer;
            juce::PluginDescription description;
            description.name = "Test Effect";
            description.pluginFormatName = "VST3";
            description.fileOrIdentifier = "/tmp/Test.vst3";
            description.uniqueId = 1234;
            writer.getKnownPluginList().addType (description);

            juce::String error;
            expect (writer.writeKnownPluginCacheAtomically (cacheFile, error), error);

            PluginManager reader;
            expect (reader.readKnownPluginCache (cacheFile, error), error);
            expectEquals (reader.getKnownPlugins().size(), 1);
            expectEquals (reader.getKnownPlugins()[0].name, juce::String ("Test Effect"));
            expect (cacheFile.deleteFile());
        }

        beginTest ("rejects an invalid custom scan root without starting a worker");
        {
            PluginManager manager;
            expect (! manager.startScan (juce::File ("/path/that/does/not/exist")));
            expect (! manager.getScanStatus().scanning);
        }

        beginTest ("scan shutdown barrier is safe when no worker is running");
        {
            PluginManager manager;
            expect (manager.cancelAndWaitForScan (100));
            expect (! manager.getScanStatus().scanning);
            expect (manager.createKnownPluginXml() != nullptr);
        }

        beginTest ("scan shutdown barrier joins a live worker before cache serialization");
        {
            const auto directory = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile ("build")
                                       .getChildFile ("empty-plugin-scan");
            expect (directory.createDirectory());
            const auto cacheFile = directory.getChildFile ("plugins.xml");
            static_cast<void> (cacheFile.deleteFile());

            PluginManager manager;
            expect (manager.startScan (directory));
            expect (manager.cancelAndWaitForScan (5000));
            expect (! manager.getScanStatus().scanning);
            juce::String error;
            expect (manager.writeKnownPluginCacheAtomically (cacheFile, error), error);
            expect (cacheFile.deleteFile());
        }
    }
};

PluginManagerTests pluginManagerTests;
} // namespace
