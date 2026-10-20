#include "LibraryReadBatch.h"

#include <atomic>
#include <mutex>

namespace
{
class ReadJob final : public juce::ThreadPoolJob
{
public:
    explicit ReadJob (std::function<void()> workToUse)
        : ThreadPoolJob ("Music metadata"), work (std::move (workToUse)) {}
    JobStatus runJob() override { work(); return jobHasFinished; }
private:
    std::function<void()> work;
};
}

void readLibraryBatch (size_t count, const std::function<bool()>& cancelled,
                       const std::function<void (size_t, TrackMetadataReader&, ArtworkStore::EncodingCache&)>& read,
                       const std::function<void (size_t, size_t)>& progress)
{
    if (count == 0 || cancelled()) return;
    if (progress) progress (0, 0);
    std::atomic_size_t next { 0 };
    std::mutex progressMutex;
    size_t completed = 0;
    auto lastProgressTime = juce::Time::getMillisecondCounterHiRes();
    const auto work = [&]
    {
        TrackMetadataReader reader;
        ArtworkStore::EncodingCache cache;
        while (! cancelled())
        {
            const auto index = next.fetch_add (1);
            if (index >= count) break;
            read (index, reader, cache);
            const std::lock_guard lock (progressMutex);
            ++completed;
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (progress && (completed == count || now - lastProgressTime >= 100.0))
            {
                progress (completed, index);
                lastProgressTime = now;
            }
        }
    };
    const auto workers = juce::jmin (count, static_cast<size_t> (juce::jlimit (1, 4, juce::SystemStats::getNumCpus())));
    if (workers == 1) { work(); return; }

    std::vector<std::unique_ptr<ReadJob>> jobs;
    juce::ThreadPool pool (juce::ThreadPool::Options().withThreadName ("Moonwake Metadata")
        .withNumberOfThreads (static_cast<int> (workers)).withDesiredThreadPriority (juce::Thread::Priority::low));
    for (size_t worker = 0; worker < workers; ++worker)
    {
        jobs.push_back (std::make_unique<ReadJob> (work));
        pool.addJob (jobs.back().get(), false);
    }
    for (const auto& job : jobs)
        static_cast<void> (pool.waitForJobToFinish (job.get(), -1));
}
