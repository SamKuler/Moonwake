#include "audio/LoudnessNormalization.h"
#include "audio/LoudnessComparison.h"
#include "audio/GaplessAudioSource.h"
#include "library/LoudnessScanService.h"
#include "library/TrackMetadataReader.h"
#include "library/TrackTagReader.h"
#include "library/LibraryRestore.h"
#include "state/AppStateStore.h"
#include "ui/LoudnessControls.h"
#include "fixtures/LibraryAudioBackend.h"
#include "fixtures/TrackMetadataFixtures.h"
#include "skin/SkinBuiltInPackages.h"
#include "skin/SkinCompiler.h"
#include "skin/SkinLayoutEngine.h"
#include <cmath>
#include <thread>

namespace
{
bool writeTone (const juce::File& file, juce::AudioChannelSet layout, float amplitude,
                int frames = 192000, bool constant = false, int onlyChannel = -1)
{
    // WAVEFORMATEXTENSIBLE names front/rear heights; JUCE's top-side enum
    // is outside that format's speaker mask. Store the corresponding front heights.
    if (layout.getChannelIndexForType (juce::AudioChannelSet::topSideLeft) >= 0)
    {
        layout.removeChannel (juce::AudioChannelSet::topSideLeft);
        layout.removeChannel (juce::AudioChannelSet::topSideRight);
        layout.addChannel (juce::AudioChannelSet::topFrontLeft);
        layout.addChannel (juce::AudioChannelSet::topFrontRight);
    }
    if (layout.getChannelIndexForType (juce::AudioChannelSet::leftSurroundRear) >= 0)
    {
        layout.removeChannel (juce::AudioChannelSet::leftSurroundRear);
        layout.removeChannel (juce::AudioChannelSet::rightSurroundRear);
        layout.addChannel (juce::AudioChannelSet::leftSurround);
        layout.addChannel (juce::AudioChannelSet::rightSurround);
    }
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
        .withSampleRate (48000).withChannelLayout (layout).withBitsPerSample (24));
    if (! writer) return false;
    juce::AudioBuffer<float> samples (layout.size(), 1024);
    for (int offset = 0; offset < frames;)
    {
        const auto count = juce::jmin (samples.getNumSamples(), frames - offset);
        for (int ch = 0; ch < layout.size(); ++ch)
            for (int i = 0; i < count; ++i)
                samples.setSample (ch, i, onlyChannel >= 0 && ch != onlyChannel ? 0.0f
                    : amplitude * (constant ? 1.0f : static_cast<float> (std::sin (
                        juce::MathConstants<double>::twoPi * 1000.0 * (offset + i) / 48000.0))));
        if (! writer->writeFromAudioSampleBuffer (samples, 0, count)) return false;
        offset += count;
    }
    return true;
}

LoudnessScanUpdate finishScan (LoudnessScanService& service)
{
    LoudnessScanUpdate result;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000;
    do
    {
        auto next = service.drainUpdates();
        result.tracks.insert (result.tracks.end(), next.tracks.begin(), next.tracks.end());
        result.running = next.running; result.failed = next.failed; result.error = next.error;
        result.completed = next.completed; result.total = next.total; result.cancelled = next.cancelled;
        if (! next.running) break;
        juce::Thread::sleep (2);
    } while (juce::Time::getMillisecondCounterHiRes() < deadline);
    return result;
}

class LoudnessNormalizationTests final : public juce::UnitTest
{
public:
    LoudnessNormalizationTests() : UnitTest ("LoudnessNormalization", "audio") {}
    void runTest() override
    {
        beginTest ("strict ReplayGain parsing and peak protection before effects");
        juce::StringPairArray tags;
        tags.set ("REPLAYGAIN_TRACK_GAIN", " +6.0 dB "); tags.set ("REPLAYGAIN_TRACK_PEAK", "0.9");
        tags.set ("REPLAYGAIN_ALBUM_GAIN", "-3 dB"); tags.set ("REPLAYGAIN_ALBUM_PEAK", "1.1");
        auto data = readReplayGainTags (tags);
        expectWithinAbsoluteError (*data.trackGainDb, 6.0, 0.0001);
        expectWithinAbsoluteError (normalizationGain (data, {}), 1.0f, 0.0001f);
        expectWithinAbsoluteError (normalizationGain (data, { LoudnessMode::track, -18, false }), 1.995262f, 0.0001f);
        expectWithinAbsoluteError (normalizationGain (data, { LoudnessMode::track, -18, true }), 0.990279f, 0.0001f);
        expectWithinAbsoluteError (normalizationGain (data, { LoudnessMode::album, -18, false }), 0.707946f, 0.0001f);
        expectWithinAbsoluteError (normalizationGain (data, { LoudnessMode::album, -18, false }, false), 1.995262f, 0.0001f);
        for (const auto* invalid : { "", "NaN", "inf", "7 nonsense", "++5 dB", "101 dB" })
        {
            tags.set ("REPLAYGAIN_TRACK_GAIN", invalid);
            expect (! readReplayGainTags (tags).trackGainDb.has_value());
        }
        data.trackPeak.reset();
        expectWithinAbsoluteError (normalizationGain (data, { LoudnessMode::track, -18, true }), 1.0f, 0.0001f);
        expectWithinAbsoluteError (normalizationGain ({}, { LoudnessMode::track, -18, false }), 1.0f, 0.0001f);

        beginTest ("ID3 TXXX ReplayGain works for v2.3/v2.4 and supported encodings");
        juce::TemporaryFile tagged (".mp3");
        for (int version : { 3, 4 })
            for (int encoding = 0; encoding <= (version == 3 ? 1 : 3); ++encoding)
            {
                auto bytes = trackMetadataFixtures::id3 (version, encoding, { "REPLAYGAIN_TRACK_GAIN", "-4.2 dB" });
                std::memcpy (static_cast<char*> (bytes.getData()) + 10, "TXXX", 4);
                expect (tagged.getFile().replaceWithData (bytes.getData(), bytes.getSize()));
                const auto parsed = readReplayGainTags (readTrackMetadataTags (tagged.getFile()));
                expect (parsed.trackGainDb.has_value());
                if (parsed.trackGainDb) expectWithinAbsoluteError (*parsed.trackGainDb, -4.2, 0.0001);
            }

        beginTest ("an existing library discovers ReplayGain on upgrade and retains same-file analysis");
        {
            auto tag = trackMetadataFixtures::id3 (3, 0, { "REPLAYGAIN_TRACK_GAIN", "-4.2 dB" });
            std::memcpy (static_cast<char*> (tag.getData()) + 10, "TXXX", 4);
            const auto audio = trackMetadataFixtures::mp3();
            const auto* bytes = static_cast<const unsigned char*> (audio.getData());
            const auto tagSize = 10 + ((bytes[6] << 21) | (bytes[7] << 14) | (bytes[8] << 7) | bytes[9]);
            juce::MemoryOutputStream combined; combined.write (tag.getData(), tag.getSize());
            combined.write (bytes + tagSize, audio.getSize() - static_cast<size_t> (tagSize));
            juce::TemporaryFile file (".mp3"); expect (file.getFile().replaceWithData (combined.getData(), combined.getDataSize()));
            TrackMetadataReader reader; auto old = reader.read (file.getFile(), false).track;
            old.loudness = {};
            MusicLibrary library; const auto id = library.upsertTrack (old); PlaylistModel queue; juce::StringArray warnings;
            juce::TemporaryFile cache (".cache");
            expect (prepareLibraryRestore (library, queue, cache.getFile(), nullptr, warnings));
            auto* restored = library.getTrack (id); expect (restored != nullptr && restored->loudness.trackGainDb.has_value());
            if (restored && restored->loudness.trackGainDb) expectWithinAbsoluteError (*restored->loudness.trackGainDb, -4.2, 0.0001);
            auto measured = *restored; measured.loudness.measured = true; measured.loudness.trackGainDb = -6.0;
            library.replaceTrack (id, measured);
            expect (prepareLibraryRestore (library, queue, cache.getFile(), nullptr, warnings));
            expectWithinAbsoluteError (*library.getTrack (id)->loudness.trackGainDb, -6.0, 0.0001);
        }
        beginTest ("preferences and file-bound measurements survive state round trip");
        PlayerAppState state;
        TrackItem track { juce::File::getCurrentWorkingDirectory().getChildFile ("loudness.wav"), "Tone", 4 };
        track.loudness = data; track.loudness.measured = true; track.loudness.albumSignature = "album-cache-key";
        state.library.upsertTrack (track);
        state.loudness = { LoudnessMode::album, -16, false };
        const auto restored = AppStateStore::fromValueTree (AppStateStore::toValueTree (state));
        expectEquals (static_cast<int> (restored.loudness.mode), static_cast<int> (LoudnessMode::album));
        expectWithinAbsoluteError (restored.loudness.targetLufs, -16.0, 0.0001);
        expect (! restored.loudness.preventClipping);
        expectEquals (restored.library.getTracks().size(), size_t { 1 });
        if (! restored.library.getTracks().empty())
        {
            const auto& saved = restored.library.getTracks()[0].loudness;
            expect (saved.measured); expectEquals (saved.albumSignature, juce::String ("album-cache-key"));
            expectWithinAbsoluteError (*saved.trackGainDb, 6.0, 0.0001);
        }
    }
} normalizationTests;

class LoudnessAnalysisTests final : public juce::UnitTest
{
public:
    LoudnessAnalysisTests() : UnitTest ("LoudnessAnalysis", "library") {}
    void runTest() override
    {
        const auto stereo = juce::AudioChannelSet::stereo();
        juce::TemporaryFile quiet (".wav"), loud (".wav"), silent (".wav");
        expect (writeTone (quiet.getFile(), stereo, 0.1f));
        expect (writeTone (loud.getFile(), stereo, 0.2f));
        expect (writeTone (silent.getFile(), stereo, 0.0f));
        TrackMetadataReader reader;
        auto a = reader.read (quiet.getFile(), false).track, b = reader.read (loud.getFile(), false).track;
        a.album = b.album = "Reference album"; a.albumArtist = b.albumArtist = "Test artist";
        beginTest ("EBU integrated track and joint album analysis preserve album dynamics");
        LoudnessScanService scan;
        expect (scan.start ({ a, b }));
        const auto result = finishScan (scan);
        expect (! result.running, result.error); expectEquals (result.failed, 0, result.error);
        std::map<juce::String, TrackItem> tracks;
        for (const auto& track : result.tracks) tracks[track.id] = track;
        expectEquals (tracks.size(), size_t { 2 });
        if (tracks.size() == 2)
        {
            const auto& ad = tracks[a.id].loudness; const auto& bd = tracks[b.id].loudness;
            expect (ad.trackGainDb && bd.trackGainDb && ad.albumGainDb && bd.albumGainDb);
            if (ad.trackGainDb && bd.trackGainDb && ad.albumGainDb && bd.albumGainDb)
            {
                expectWithinAbsoluteError (*ad.trackGainDb - *bd.trackGainDb, 6.0206, 0.03);
                expectWithinAbsoluteError (*ad.albumGainDb, *bd.albumGainDb, 0.0001);
                expectWithinAbsoluteError (*ad.albumGainDb, *ad.trackGainDb - 3.9794, 0.15);
                expectEquals (ad.albumSignature, loudnessAlbumSignature ({ a, b }));
                expect (ad.albumSignature != loudnessAlbumSignature ({ a }));
                expectWithinAbsoluteError (*ad.trackPeak, 0.1, 0.002);
            }
        }
        beginTest ("album identity follows library grouping across disc folders and artist fallbacks");
        {
            auto disc1 = a, disc2 = b;
            disc1.file = quiet.getFile().getParentDirectory().getChildFile ("CD1/first.wav");
            disc2.file = loud.getFile().getParentDirectory().getChildFile ("CD2/second.wav");
            disc1.discNumber = 1; disc2.discNumber = 2;
            expectEquals (loudnessAlbumKey (disc1), loudnessAlbumKey (disc2));
            disc1.albumArtist.clear(); disc2.albumArtist.clear(); disc1.artist = "First artist"; disc2.artist = "Second artist";
            expect (loudnessAlbumKey (disc1) != loudnessAlbumKey (disc2));
        }
        beginTest ("silence reports an analysis failure and leaves existing metadata intact");
        auto zero = reader.read (silent.getFile(), false).track;
        zero.loudness.trackGainDb = -5.0;
        expect (scan.start ({ zero }));
        const auto failure = finishScan (scan);
        expectEquals (failure.failed, 1); expect (failure.tracks.empty()); expect (failure.error.isNotEmpty());

        beginTest ("cancellation retains completed track measurements");
        std::vector<TrackItem> many { a };
        for (int i = 0; i < 100; ++i) { auto copy = b; copy.id = juce::Uuid().toString(); many.push_back (copy); }
        const auto existingAlbum = loudnessAlbumSignature (many);
        for (auto& track : many)
        {
            track.loudness.measured = true;
            track.loudness.albumSignature = existingAlbum;
            track.loudness.albumGainDb = -7.0;
        }
        expect (scan.start (many));
        LoudnessScanUpdate partial;
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
        do { partial = scan.drainUpdates(); if (! partial.tracks.empty()) break; juce::Thread::sleep (1); }
        while (juce::Time::getMillisecondCounterHiRes() < deadline);
        expect (! partial.tracks.empty());
        if (! partial.tracks.empty())
        {
            expect (partial.tracks[0].loudness.albumGainDb.has_value());
            if (partial.tracks[0].loudness.albumGainDb)
                expectWithinAbsoluteError (*partial.tracks[0].loudness.albumGainDb, -7.0, 0.0001);
        }
        scan.cancel();
        const auto cancelled = finishScan (scan);
        expect (cancelled.cancelled); expect (cancelled.completed < cancelled.total);

        beginTest ("speaker layouts measure 2, 6, 8 and 10 channels; LFE is excluded");
        for (const auto& layout : { stereo, juce::AudioChannelSet::create5point1(),
                                   juce::AudioChannelSet::create5point1point2(), juce::AudioChannelSet::create7point1point2() })
        {
            juce::TemporaryFile file (".wav");
            expect (writeTone (file.getFile(), layout, 0.1f, 48000));
            auto item = reader.read (file.getFile(), false).track;
            expect (scan.start ({ item })); const auto measured = finishScan (scan);
            expectEquals (measured.failed, 0, measured.error);
            expect (! measured.tracks.empty());
            if (! measured.tracks.empty()) expect (measured.tracks.back().loudness.trackGainDb.has_value());
            if (layout.size() > 2)
            {
                juce::TemporaryFile lfe (".wav");
                expect (writeTone (lfe.getFile(), layout, 0.2f, 48000, false, layout.getChannelIndexForType (juce::AudioChannelSet::LFE)));
                expect (scan.start ({ reader.read (lfe.getFile(), false).track }));
                const auto excluded = finishScan (scan); expectEquals (excluded.failed, 1);
            }
        }
    }
} analysisTests;

class LoudnessGaplessTests final : public juce::UnitTest
{
public:
    LoudnessGaplessTests() : UnitTest ("LoudnessGapless", "audio") {}
    void runTest() override
    {
        beginTest ("a callback crossing tracks applies each track gain to the matching samples");
        for (const auto& layout : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::create5point1(),
                                   juce::AudioChannelSet::create5point1point2(), juce::AudioChannelSet::create7point1point2() })
        {
            juce::TemporaryFile a (".wav"), b (".wav");
            expect (writeTone (a.getFile(), layout, 0.1f, 600, true));
            expect (writeTone (b.getFile(), layout, 0.2f, 600, true));
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            juce::TimeSliceThread worker ("Gapless normalization test"); worker.startThread();
            GaplessAudioSource source (formats, worker);
            source.setDestinationLayout (layout);
            auto gainA = std::make_shared<std::atomic<float>> (0.5f), gainB = std::make_shared<std::atomic<float>> (0.25f);
            expect (source.loadPlan ({ { { a.getFile(), 1, gainA }, { b.getFile(), 2, gainB } }, -1 }));
            source.prepareToPlay (512, 48000);
            expect (source.setUpcoming ({ { b.getFile(), 2, gainB } }, -1));
            juce::AudioBuffer<float> block (layout.size(), 512);
            source.getNextAudioBlock ({ &block, 0, 512 });
            source.getNextAudioBlock ({ &block, 0, 512 });
            for (int ch = 0; ch < layout.size(); ++ch)
                for (int sample : { 24, 60, 124, 300, 480 })
                    expectWithinAbsoluteError (block.getSample (ch, sample),
                        layout.getTypeOfChannel (ch) == juce::AudioChannelSet::topSideLeft
                        || layout.getTypeOfChannel (ch) == juce::AudioChannelSet::topSideRight ? 0.025f : 0.05f, 0.001f);
        }
    }
} gaplessTests;

class ComparisonPlugin final : public juce::AudioPluginInstance
{
public:
    ComparisonPlugin() : AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}
    int calls = 0;
    bool doubleOnlyLeft = false;
    const juce::String getName() const override { return "Comparison gain and delay"; }
    void fillInPluginDescription (juce::PluginDescription&) const override {}
    void prepareToPlay (double, int) override { history.setSize (getTotalNumOutputChannels(), 256); history.clear(); index = 0; setLatencySamples (256); }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override
    {
        ++calls;
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                const auto input = audio.getSample (ch, i);
                audio.setSample (ch, i, history.getSample (ch, index) * (! doubleOnlyLeft || ch == 0 ? 2.0f : 1.0f));
                history.setSample (ch, index, input);
            }
            index = (index + 1) % 256;
        }
    }
    bool isBusesLayoutSupported (const BusesLayout& buses) const override { return buses.getMainInputChannelSet() == buses.getMainOutputChannelSet(); }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; } bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; } bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {} const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {} void setStateInformation (const void*, int) override {}
private:
    juce::AudioBuffer<float> history; int index = 0;
};

class LoudnessComparisonTests final : public juce::UnitTest
{
public:
    LoudnessComparisonTests() : UnitTest ("LoudnessComparison", "audio") {}
    void runTest() override
    {
        beginTest ("gain-matched, latency-aligned A/B preserves playback and plugin processing in 2/6/8/10 channels");
        for (auto layout : { OutputLayout::stereo, OutputLayout::fiveOne, OutputLayout::fiveOneTwo, OutputLayout::sevenOneTwo })
        {
            auto backend = std::make_unique<LibraryAudioBackend>(); auto* device = backend.get();
            AudioEngine engine (std::move (backend)); expect (engine.initialise().isEmpty());
            expect (engine.setOutputLayout (layout).isEmpty());
            const auto channels = toAudioChannelSet (layout);
            juce::TemporaryFile file (".wav"); expect (writeTone (file.getFile(), channels, 0.1f, 480000));
            auto effect = std::make_unique<ComparisonPlugin>(); auto* probe = effect.get(); juce::String error;
            expect (engine.addEffectPlugin (1, std::move (effect), error), error);
            expect (engine.loadFile (file.getFile())); engine.setGain (1); engine.play();
            const auto attachments = device->callbackAttachments;
            engine.setLoudnessComparisonEnabled (true);
            juce::AudioBuffer<float> output (channels.size(), 256);
            LoudnessComparisonStatus status;
            // Feed the asynchronous workers at the 48 kHz / 256-frame device cadence.
            for (int block = 0; block < 720 && ! status.matched; ++block)
            {
                device->render (output); juce::Thread::sleep (6);
                if (block % 4 == 0) status = engine.getLoudnessComparisonStatus();
            }
            for (int wait = 0; wait < 1000 && ! status.matched && ! status.interrupted; ++wait)
            { juce::Thread::sleep (2); status = engine.getLoudnessComparisonStatus(); }
            status = engine.getLoudnessComparisonStatus(); expect (status.matched, "seconds=" + juce::String (status.measuredSeconds, 4) + " interrupted=" + juce::String (status.interrupted ? 1 : 0));
            expectWithinAbsoluteError (status.wetGainDb, -6.0206, 0.12);
            expectWithinAbsoluteError (status.dryGainDb, 0.0, 0.001);
            for (int i = 0; i < 8; ++i) device->render (output);
            const auto processed = output.getRMSLevel (0, 0, 256);
            const auto calls = probe->calls; const auto before = engine.getPlaybackProgress();
            engine.setAllEffectsBypassed (true);
            for (int i = 0; i < 8; ++i) device->render (output);
            expectWithinAbsoluteError (output.getRMSLevel (0, 0, 256), processed, 0.003f);
            expect (probe->calls > calls); expect (engine.getPlaybackProgress().positionSeconds > before.positionSeconds);
            expectEquals (device->callbackAttachments, attachments);
            engine.resetOutputMeter(); expect (engine.getLoudnessComparisonStatus().matched);
            expect (engine.seekToSeconds (0.0)); expect (! engine.getLoudnessComparisonStatus().matched);
            engine.resetLoudnessComparison(); expect (! engine.getLoudnessComparisonStatus().matched);
            engine.setLoudnessComparisonEnabled (false); expect (! engine.getLoudnessComparisonStatus().enabled);
        }
        beginTest ("muted and disconnected channels are excluded from audible A/B matching");
        for (bool disconnect : { false, true })
        {
            auto backend = std::make_unique<LibraryAudioBackend>(); auto* device = backend.get();
            AudioEngine engine (std::move (backend)); expect (engine.initialise().isEmpty());
            juce::TemporaryFile file (".wav"); expect (writeTone (file.getFile(), juce::AudioChannelSet::stereo(), 0.1f, 480000));
            auto effect = std::make_unique<ComparisonPlugin>(); effect->doubleOnlyLeft = true; juce::String error;
            expect (engine.addEffectPlugin (1, std::move (effect), error), error);
            if (disconnect) expect (engine.setOutputRouting ({ -1, 1 }).isEmpty());
            else expect (engine.setOutputChannelMix (juce::AudioChannelSet::left, 1.0f, true, false));
            expect (engine.loadFile (file.getFile())); engine.play(); engine.setLoudnessComparisonEnabled (true);
            juce::AudioBuffer<float> output (2, 256); LoudnessComparisonStatus status;
            for (int block = 0; block < 720 && ! status.matched; ++block)
            { device->renderPhysicalOutputs (output); juce::Thread::sleep (6); if (block % 4 == 0) status = engine.getLoudnessComparisonStatus(); }
            for (int wait = 0; wait < 1000 && ! status.matched && ! status.interrupted; ++wait)
            { juce::Thread::sleep (2); status = engine.getLoudnessComparisonStatus(); }
            status = engine.getLoudnessComparisonStatus(); expect (status.matched, "seconds=" + juce::String (status.measuredSeconds, 4) + " interrupted=" + juce::String (status.interrupted ? 1 : 0));
            expectWithinAbsoluteError (status.wetGainDb, 0.0, 0.02);
            expectWithinAbsoluteError (status.dryGainDb, 0.0, 0.02);
            if (disconnect) expect (engine.setOutputRouting ({ 0, 1 }).isEmpty());
            else expect (engine.setOutputChannelMix (juce::AudioChannelSet::left, 1.0f, false, false));
            expect (! engine.getLoudnessComparisonStatus().matched);
        }
        beginTest ("concurrent measurement resets keep the dry and wet windows paired");
        {
            LoudnessComparison comparison; comparison.prepare (48000, juce::AudioChannelSet::stereo()); comparison.setEnabled (true);
            std::atomic_bool finished { false };
            std::thread producer ([&]
            {
                juce::AudioBuffer<float> dry (2, 256), wet (2, 256);
                for (int block = 0; block < 900; ++block)
                {
                    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i)
                    { const auto value = 0.1f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 1000 * (block * 256 + i) / 48000)); dry.setSample (ch, i, value); wet.setSample (ch, i, value * 2); }
                    const auto epoch = comparison.beginBlock(); comparison.push (dry, wet, 256, epoch); juce::Thread::sleep (6);
                }
                finished.store (true);
            });
            for (int reset = 0; reset < 60; ++reset) { comparison.reset(); juce::Thread::sleep (6); }
            LoudnessComparisonStatus status;
            while (! finished.load()) { status = comparison.update(); juce::Thread::sleep (2); }
            producer.join(); status = comparison.update(); expect (status.matched);
            expectWithinAbsoluteError (status.wetGainDb, -6.0206, 0.04);
        }
        beginTest ("silence never becomes a matched comparison");
        LoudnessComparison comparison; comparison.prepare (48000, juce::AudioChannelSet::stereo()); comparison.setEnabled (true);
        juce::AudioBuffer<float> zero (2, 512); zero.clear();
        for (int i = 0; i < 300; ++i) { const auto block = comparison.beginBlock(); comparison.push (zero, zero, 512, block); juce::Thread::sleep (11); }
        const auto silence = comparison.update();
        expect (! silence.matched); expect (! silence.interrupted);
        expect (silence.measuredSeconds >= 3.0);
    }
} comparisonTests;


class LoudnessControllerTests final : public juce::UnitTest
{
public:
    LoudnessControllerTests() : UnitTest ("LoudnessController", "app") {}
    void runTest() override
    {
        beginTest ("background analysis updates the current playback gain without rebuilding the device or queue");
        juce::TemporaryFile file (".wav"); expect (writeTone (file.getFile(), juce::AudioChannelSet::stereo(), 0.1f, 480000));
        auto backend = std::make_unique<LibraryAudioBackend>(); auto* device = backend.get();
        PlayerController controller (std::move (backend));
        controller.addFiles ({ file.getFile() }); expect (controller.selectTrack (0)); expect (controller.setPlaying (true));
        const auto trackId = controller.getLoadedTrackId();
        const auto version = controller.getStateRevision();
        const auto attachments = device->callbackAttachments;
        controller.setLoudnessPreferences ({ LoudnessMode::track, -23, false }); expect (controller.startLoudnessScan());
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000;
        while (controller.getLoudnessScanStatus().running && juce::Time::getMillisecondCounterHiRes() < deadline)
        { controller.synchronizePlayback(); juce::Thread::sleep (2); }
        controller.synchronizePlayback(); expect (! controller.getLoudnessScanStatus().running);
        expectEquals (controller.getMeasuredTrackCount(), 1);
        juce::AudioBuffer<float> output (2, 256);
        for (int block = 0; block < 16; ++block) device->render (output);
        const auto normalized = output.getRMSLevel (0, 0, output.getNumSamples());
        expectWithinAbsoluteError (normalized, 0.04f, 0.002f);
        expectEquals (controller.getLoadedTrackId(), trackId); expectEquals (device->callbackAttachments, attachments);
        // Open PCM retains the gain for its original source revision.
        juce::TemporaryFile moved (".wav"); expect (writeTone (moved.getFile(), juce::AudioChannelSet::stereo(), 0.5f, 480000));
        TrackMetadataReader metadata; auto replacement = metadata.read (moved.getFile(), false);
        replacement.track.loudness.trackGainDb = -12.0;
        expect (controller.relocateLibraryTrack (controller.getLibrary().getTracks()[0].id, replacement));
        for (int block = 0; block < 16; ++block) device->render (output);
        expectWithinAbsoluteError (output.getRMSLevel (0, 0, 256), normalized, 0.003f);
        controller.setLoudnessPreferences ({ LoudnessMode::off, -23, false });
        for (int block = 0; block < 16; ++block) device->render (output);
        expect (output.getRMSLevel (0, 0, output.getNumSamples()) > normalized * 1.3f);
        expect (controller.getStateRevision() > version);
    }
} controllerTests;

class LoudnessControlsTests final : public juce::UnitTest
{
public:
    LoudnessControlsTests() : UnitTest ("LoudnessControls", "ui") {}
    void runTest() override
    {
        beginTest ("skin-bound settings and comparison controls own their semantic state");
        PlayerController controller (std::make_unique<LibraryAudioBackend>());
        LoudnessControls controls (controller);
        juce::Component root; controls.addTo (root);
        SkinComponentBindings bindings; SkinValidationReport report; controls.bind (bindings, report);
        expect (! report.hasErrors(), report.toText());
        auto* mode = dynamic_cast<juce::ComboBox*> (bindings.find ("settings.loudness.mode"));
        auto* target = dynamic_cast<juce::ComboBox*> (bindings.find ("settings.loudness.target"));
        auto* clipping = dynamic_cast<juce::ToggleButton*> (bindings.find ("settings.loudness.preventClipping"));
        expect (mode && target && clipping);
        if (mode && target && clipping)
        {
            controls.refresh(); expect (! target->isEnabled());
            mode->setSelectedId (2, juce::dontSendNotification); mode->onChange(); controls.refresh();
            expect (target->isEnabled());
            expectEquals (static_cast<int> (controller.getLoudnessPreferences().mode), static_cast<int> (LoudnessMode::track));
            target->setSelectedId (14, juce::dontSendNotification); target->onChange();
            expectWithinAbsoluteError (controller.getLoudnessPreferences().targetLufs, -14.0, 0.0001);
            controls.refresh (false); expect (! mode->isEnabled()); expect (! clipping->isEnabled());
        }
        beginTest ("all shipped skins expose loudness controls without clipping at supported window sizes");
        SkinValidationReport foundationReport;
        const auto package = SkinBuiltInPackages::create (SkinBuiltInPackages::Package::foundation, foundationReport);
        const auto foundation = package ? SkinCompiler::compile (*package, {}, foundationReport) : nullptr;
        expect (foundation != nullptr, foundationReport.toText());
        for (const auto kind : { SkinBuiltInPackages::Package::foundation, SkinBuiltInPackages::Package::defaultSkin })
        {
            SkinValidationReport validation;
            const auto skinPackage = SkinBuiltInPackages::create (kind, validation);
            const auto skin = skinPackage ? SkinCompiler::compile (*skinPackage,
                kind == SkinBuiltInPackages::Package::foundation ? nullptr : foundation, validation) : nullptr;
            expect (skin != nullptr, validation.toText());
            if (! skin) continue;
            for (const auto bounds : { juce::Rectangle<float> (0, 0, 1040, 700), juce::Rectangle<float> (0, 0, 1240, 800) })
                for (const auto workspace : { WorkspaceMode::studio, WorkspaceMode::listening })
                    for (const auto entry : { SkinLayoutEntry::studio, SkinLayoutEntry::effects, SkinLayoutEntry::settings })
                    {
                        SkinValidationReport layoutReport;
                        const auto layout = SkinLayoutEngine::resolve (skin->layouts.at (entry), bounds, 1.0f,
                            layoutReport, workspace, skin.get());
                        expect (layout.has_value(), layoutReport.toText());
                        if (! layout) continue;
                        const juce::StringArray ids = entry == SkinLayoutEntry::settings
                            ? juce::StringArray { "settings.loudness.mode", "settings.loudness.target", "settings.loudness.analyze" }
                            : juce::StringArray { "effects.compare.original", "effects.compare.processed", "effects.compare.match", "effects.compare.reset" };
                        for (const auto& id : ids)
                        {
                            const auto* node = layout->component (id); expect (node != nullptr, id);
                            if (node) expect (node->bounds.getWidth() >= 28 && node->bounds.getHeight() >= 28, id);
                        }
                    }
        }
    }
} controlsTests;
} // namespace
