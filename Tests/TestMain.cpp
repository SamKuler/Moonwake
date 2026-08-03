#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "skin/SkinInstaller.h"
#include "skin/SkinPackage.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdlib>

namespace
{
int runSkinInstallWorker (char* argv[])
{
    const juce::File source (argv[2]);
    const juce::File snapshots (argv[3]);
    const juce::File installs (argv[4]);
    const juce::File barrier (argv[5]);
    const juce::String workerId (argv[6]);

    SkinValidationReport report;
    const auto foundationPackage = SkinBuiltInPackages::create (
        SkinBuiltInPackages::Package::foundation, report);
    const auto foundation = foundationPackage
        ? SkinCompiler::compile (*foundationPackage, {}, report) : nullptr;
    const auto package = SkinPackage::captureSnapshot (
        source, snapshots.getChildFile (workerId), report);
    const auto compiled = package && foundation
        ? SkinCompiler::compile (*package, foundation, report) : nullptr;
    if (! package || ! compiled) return 3;

    const auto ready = barrier.getChildFile (workerId + ".ready");
    if (! ready.replaceWithText ("ready")) return 4;
    const auto start = barrier.getChildFile ("start");
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (! start.existsAsFile()
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep (1);
    if (! start.existsAsFile()) return 5;

    SkinInstaller installer (installs);
    return installer.install (*package, compiled->manifest, {}, report) ? 0 : 6;
}

int runPluginScanProbe (const juce::String& mode)
{
    if (mode == "success")
        return 0;

    if (mode == "failure")
        return 73;

    if (mode == "hang")
    {
        juce::Thread::sleep (10000);
        return 0;
    }

    return 74;
}

int runPluginScannerFixture (int argc, char* argv[])
{
    int protocolIndex = -1;
    for (int index = 3; index < argc; ++index)
        if (juce::String (argv[index]) == "--scan")
        {
            protocolIndex = index;
            break;
        }

    if (protocolIndex < 0 || protocolIndex + 3 >= argc)
        return 75;

    const juce::String mode (argv[2]);
    if (mode == "failure")
        return 76;

    if (mode == "hang")
    {
        juce::Thread::sleep (10000);
        return 0;
    }

    const auto writeResult = [&]
    {
        juce::KnownPluginList result;
        juce::PluginDescription description;
        description.name = "Fixture Effect";
        description.manufacturerName = "Moonwake Tests";
        description.pluginFormatName = argv[protocolIndex + 1];
        description.fileOrIdentifier = argv[protocolIndex + 2];
        description.uniqueId = description.fileOrIdentifier.hashCode();
        result.addType (description);
        const auto xml = result.createXml();
        return xml != nullptr
            && xml->writeTo (juce::File (argv[protocolIndex + 3]));
    };

    const auto waitForFile = [] (const juce::File& file)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (! file.existsAsFile()
               && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::Thread::sleep (1);
        return file.existsAsFile();
    };

    if (mode == "collision-first" || mode == "collision-second")
    {
        if (protocolIndex != 4)
            return 79;
        const juce::File barrier (argv[3]);
        const auto firstReady = barrier.getChildFile ("first.ready");
        const auto secondReady = barrier.getChildFile ("second.ready");
        const auto firstWritten = barrier.getChildFile ("first.written");
        const auto secondWritten = barrier.getChildFile ("second.written");

        if (mode == "collision-first")
        {
            if (! firstReady.replaceWithText ("ready")
                || ! waitForFile (secondReady)
                || ! writeResult()
                || ! firstWritten.replaceWithText ("written")
                || ! waitForFile (secondWritten))
                return 80;
            juce::Thread::sleep (100);
            return 0;
        }

        if (! secondReady.replaceWithText ("ready")
            || ! waitForFile (firstWritten)
            || ! writeResult()
            || ! secondWritten.replaceWithText ("written"))
            return 81;
        return 0;
    }

    if (mode != "success")
        return 77;

    return writeResult() ? 0 : 78;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialiser;

    if (argc == 7 && juce::String (argv[1]) == "--skin-install-worker")
        return runSkinInstallWorker (argv);

    if (argc == 3 && juce::String (argv[1]) == "--plugin-scan-probe")
        return runPluginScanProbe (argv[2]);

    if (argc >= 2 && juce::String (argv[1]) == "--plugin-scan-fixture")
        return runPluginScannerFixture (argc, argv);

    juce::UnitTestRunner runner;

    if (argc == 1)
    {
        runner.runAllTests();
    }
    else
    {
        if (argc != 3 || juce::String (argv[1]) != "--filter")
            return 2;

        const auto filter = juce::String (argv[2]);
        if (filter.isEmpty())
            return 2;

        juce::Array<juce::UnitTest*> selected;
        for (auto* test : juce::UnitTest::getAllTests())
            if (test != nullptr && test->getName().containsIgnoreCase (filter))
                selected.add (test);

        if (selected.isEmpty())
            return 3;

        runner.runTests (selected);
    }

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i); result != nullptr && result->failures > 0)
            return 1;

    return 0;
}
