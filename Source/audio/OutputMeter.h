#pragma once

#include "audio/OutputLayout.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>

struct OutputMeterReadings
{
    double momentaryLufs = -std::numeric_limits<double>::infinity();
    double shortTermLufs = -std::numeric_limits<double>::infinity();
    double integratedLufs = -std::numeric_limits<double>::infinity();
    // Linear amplitudes. Peaks release at 20 dB/s; RMS uses a 300 ms window.
    std::array<float, maximumOutputChannels> samplePeak {}, rms {}, truePeak {}, maxTruePeak {};
    int channels = 0;
    double measuredSeconds = 0.0;
    bool available = false;
    bool measurementInterrupted = false;
    uint64_t droppedFrames = 0;
};

// One audio producer and one measurement worker. Configuration/release require
// the audio producer to be stopped; reset may run concurrently with it.
class OutputMeter final
{
public:
    OutputMeter();
    ~OutputMeter();
    void prepare (double sampleRate, const juce::AudioChannelSet&);
    void release();
    void reset() noexcept;
    // Capture before rendering a device block, so a concurrent seek/reset cannot
    // relabel an already-rendered block as audio from the new measurement.
    [[nodiscard]] uint64_t beginBlock() const noexcept;
    void push (const juce::AudioBuffer<float>&, int startSample, int numSamples,
               uint64_t blockEpoch) noexcept;
    // Control/UI thread only; never called by the audio producer.
    [[nodiscard]] OutputMeterReadings getReadings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
