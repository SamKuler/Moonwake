#include "library/LibraryReadBatch.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>

namespace
{
class LibraryReadBatchTests final : public juce::UnitTest
{
public:
    LibraryReadBatchTests() : UnitTest ("LibraryReadBatch", "library") {}

    void runTest() override
    {
        beginTest ("bounded workers overlap reads and own independent readers and cover caches");
        {
            const auto expected = juce::jlimit (1, 4, juce::SystemStats::getNumCpus());
            std::mutex mutex;
            std::condition_variable started;
            int active = 0, peak = 0;
            std::set<TrackMetadataReader*> readers;
            std::set<ArtworkStore::EncodingCache*> caches;
            std::vector<int> output (16, -1);
            readLibraryBatch (output.size(), [] { return false; },
                [&] (size_t index, TrackMetadataReader& reader, ArtworkStore::EncodingCache& cache)
                {
                    {
                        std::unique_lock lock (mutex);
                        peak = juce::jmax (peak, ++active);
                        readers.insert (&reader);
                        caches.insert (&cache);
                        started.notify_all();
                        if (index < static_cast<size_t> (expected))
                            started.wait_for (lock, std::chrono::milliseconds (1000), [&] { return peak == expected; });
                    }
                    output[index] = static_cast<int> (index);
                    { const std::lock_guard lock (mutex); --active; }
                });
            expectEquals (peak, expected);
            expectEquals (static_cast<int> (readers.size()), expected);
            expectEquals (static_cast<int> (caches.size()), expected);
            expectEquals (active, 0, "Every worker has returned before the batch returns.");
            for (size_t index = 0; index < output.size(); ++index)
                expectEquals (output[index], static_cast<int> (index));
        }

        beginTest ("cancellation joins in-flight reads and stops claiming pending work");
        {
            std::atomic_bool cancelled { false };
            std::atomic_int calls { 0 }, active { 0 };
            readLibraryBatch (100, [&] { return cancelled.load(); },
                [&] (size_t, TrackMetadataReader&, ArtworkStore::EncodingCache&)
                {
                    ++active;
                    ++calls;
                    cancelled.store (true);
                    juce::Thread::sleep (5);
                    --active;
                });
            expect (calls.load() >= 1 && calls.load() <= 4);
            expectEquals (active.load(), 0);
            const auto before = calls.load();
            readLibraryBatch (100, [&] { return cancelled.load(); },
                [&] (size_t, TrackMetadataReader&, ArtworkStore::EncodingCache&) { ++calls; });
            expectEquals (calls.load(), before);
        }

        beginTest ("progress counts completed files monotonically and empty batches do no work");
        {
            std::vector<size_t> progress;
            std::atomic_int calls { 0 };
            readLibraryBatch (9, [] { return false; },
                [&] (size_t, TrackMetadataReader&, ArtworkStore::EncodingCache&) { ++calls; },
                [&] (size_t completed, size_t index)
                {
                    progress.push_back (completed);
                    expect (index < 9);
                });
            expectEquals (calls.load(), 9);
            expect (! progress.empty() && progress.front() == 0 && progress.back() == 9);
            expect (std::is_sorted (progress.begin(), progress.end()));
            readLibraryBatch (0, [] { return false; },
                [&] (size_t, TrackMetadataReader&, ArtworkStore::EncodingCache&) { ++calls; });
            expectEquals (calls.load(), 9);
        }
    }
};
LibraryReadBatchTests libraryReadBatchTests;
}
