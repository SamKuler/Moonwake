#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "library/LibraryImportService.h"

namespace
{
class LibraryImportServiceTests final : public juce::UnitTest
{
public:
    LibraryImportServiceTests()
        : UnitTest ("LibraryImportService", "library") {}

    void runTest() override
    {
        beginTest ("discovers supported files recursively and deduplicates inputs");
        {
            const auto root = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-import", {}, true);
            const auto nested = root.getChildFile ("Album");
            expect (nested.createDirectory());
            const auto first = root.getChildFile ("b.WAV");
            const auto second = nested.getChildFile ("a.flac");
            expect (first.replaceWithText ("fixture"));
            expect (second.replaceWithText ("fixture"));
            expect (nested.getChildFile ("notes.txt").replaceWithText ("ignore"));

            juce::Array<juce::File> inputs { root, first, nested };
            const auto files = LibraryImportService::collectAudioFiles (inputs);
            expectEquals (files.size(), 2);
            expectEquals (files[0].getFileName(), juce::String ("a.flac"));
            expectEquals (files[1].getFileName(), juce::String ("b.WAV"));
            expect (root.deleteRecursively());
        }

        beginTest ("deduplicates direct symbolic-link aliases by file identity");
        {
            const auto root = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-import-alias", {}, true);
            expect (root.createDirectory());
            const auto target = root.getChildFile ("target.wav");
            const auto alias = root.getChildFile ("alias.wav");
            expect (target.replaceWithText ("fixture"));
            expect (target.createSymbolicLink (alias, true));
            const auto files = LibraryImportService::collectAudioFiles (
                { target, alias });
            expectEquals (files.size(), 1);
            expect (root.deleteRecursively());
        }

        beginTest ("stops discovery at configured safety limits");
        {
            const auto root = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-import-limit", {}, true);
            expect (root.createDirectory());
            expect (root.getChildFile ("one.wav").replaceWithText ("fixture"));
            expect (root.getChildFile ("two.wav").replaceWithText ("fixture"));
            juce::StringArray skipped;
            const auto files = LibraryImportService::collectAudioFiles (
                { root }, nullptr, &skipped, { 1, 100, 10 });
            expectEquals (files.size(), 1);
            expect (skipped.joinIntoString (" ").containsIgnoreCase ("limit"));
            expect (root.deleteRecursively());
        }

        beginTest ("reports missing roots and honours cancellation");
        {
            const auto missing = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-import-missing", {}, true);
            juce::StringArray skipped;
            juce::Array<juce::File> inputs { missing };
            const auto files = LibraryImportService::collectAudioFiles (
                inputs, nullptr, &skipped);
            expect (files.isEmpty());
            expectEquals (skipped.size(), 1);

            std::atomic_bool cancelled { true };
            skipped.clear();
            expect (LibraryImportService::collectAudioFiles (
                inputs, &cancelled, &skipped).isEmpty());
            expect (skipped.isEmpty());
        }

        beginTest ("stays busy until its queued completion has been consumed");
        {
            const auto missing = juce::File::getSpecialLocation (
                juce::File::tempDirectory).getNonexistentChildFile (
                    "moonwake-import-lifecycle", ".wav", true);
            const juce::Array<juce::File> inputs { missing };
            LibraryImportService service;
            bool firstCompleted = false;
            bool busyDuringCompletion = false;
            bool overlapRejected = false;
            bool secondStarted = false;
            bool secondCompleted = false;

            expect (service.start (
                inputs, {},
                [&] (LibraryImportResult)
                {
                    firstCompleted = true;
                    busyDuringCompletion = service.isRunning();
                    overlapRejected = ! service.start (
                        inputs, {}, [] (LibraryImportResult) {});
                    juce::MessageManager::callAsync ([&]
                    {
                        secondStarted = service.start (
                            inputs, {},
                            [&] (LibraryImportResult)
                            {
                                secondCompleted = true;
                            });
                    });
                }));

            const auto deadline = juce::Time::getMillisecondCounterHiRes()
                                + 2000.0;
            while (! secondCompleted
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

            expect (firstCompleted);
            expect (busyDuringCompletion);
            expect (overlapRejected);
            expect (secondStarted);
            expect (secondCompleted);
        }
    }
};

LibraryImportServiceTests libraryImportServiceTests;
} // namespace
