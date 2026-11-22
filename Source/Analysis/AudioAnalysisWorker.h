#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "PitchDetector.h"
#include "KeyEstimator.h"

namespace magicalchords
{
// Single-producer (audio callback), single-consumer (worker) capture queue.
// All memory is allocated before audio starts; processBlock only copies samples.
//
// Thread model:
//   * pushAudio() runs on the real-time audio thread. It appends to the FIFO
//     and to a lock-free 256-sample live-waveform ring (atomic write index).
//   * run() runs on an internal juce::Thread. It drains the FIFO, runs the
//     YIN PitchDetector, accumulates a pitch-class histogram and publishes
//     frequency / confidence / tonic / minor through std::atomic.
//   * reset() is called from the UI thread. It disarms capture and sets a
//     resetRequested flag that the worker honours at the top of its next
//     iteration, so worker-owned state is always cleared by the worker itself
//     (no cross-thread mutation of non-atomic worker state).
class AudioAnalysisWorker final : private juce::Thread
{
public:
    static constexpr int kLiveWaveformSize = 256;

    AudioAnalysisWorker();
    ~AudioAnalysisWorker() override;

    void prepare(double sampleRate);
    void setCapturing(bool shouldCapture) noexcept;
    bool isCapturing() const noexcept;
    void pushAudio(const float* monoSamples, int numSamples) noexcept;

    // Async: requests the worker thread to clear all analysis state. Safe to
    // call from the UI thread. Disarms capture immediately.
    void reset() noexcept;

    double latestFrequencyHz() const noexcept;
    float latestConfidence() const noexcept;
    int analyzedWindowCount() const noexcept;

    // Key estimation results (Krumhansl-Schmuckler), published from the worker.
    int detectedTonic() const noexcept;       // pitch class 0..11
    bool detectedMinor() const noexcept;      // true => natural minor

    // Most recent 256 mono samples, oldest first, for the editor's preview.
    void copyLiveWaveform(std::array<float, kLiveWaveformSize>& dest) const noexcept;

private:
    void run() override;
    void handleResetRequest() noexcept;
    void updateKeyEstimate (double frequencyHz, float confidence) noexcept;

    static constexpr int queueCapacity = 262144;
    static constexpr int analysisWindowSize = 8192;
    static constexpr int analysisHopSize = 1024;
    static constexpr int keyRecomputeInterval = 8; // confident frames per key update

    juce::AbstractFifo fifo { queueCapacity };
    std::vector<float> queue;
    std::vector<float> window;
    std::vector<float> analysisFrame;
    int writePosition = 0;
    std::vector<float> scratch;
    PitchDetector pitchDetector;
    KeyEstimator keyEstimator;

    std::atomic<bool>   capturing { false };
    std::atomic<bool>   resetRequested { false };
    std::atomic<double> sampleRateHz { 44100.0 };
    std::atomic<double> frequencyHz { 0.0 };
    std::atomic<float>  confidence { 0.0f };
    std::atomic<int>    windowCount { 0 };
    std::atomic<int>    tonic { 0 };
    std::atomic<bool>   minor { false };

    int framesSinceKeyUpdate = 0;

    // Lock-free single-producer (audio) / single-consumer (UI) live waveform.
    std::array<float, kLiveWaveformSize> liveWave {};
    std::atomic<int> liveWritePos { 0 };
    std::atomic<int> liveFillCount { 0 };
};
}
