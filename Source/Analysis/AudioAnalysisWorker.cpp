#include "AudioAnalysisWorker.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace magicalchords
{
AudioAnalysisWorker::AudioAnalysisWorker()
    : juce::Thread ("Magical Chords audio analysis"),
      queue (static_cast<size_t> (queueCapacity), 0.0f),
      window (static_cast<size_t> (analysisWindowSize), 0.0f),
      analysisFrame (static_cast<size_t> (analysisWindowSize), 0.0f),
      scratch (2048, 0.0f)
{
    startThread();
}

AudioAnalysisWorker::~AudioAnalysisWorker()
{
    signalThreadShouldExit();
    stopThread (2000);
}

void AudioAnalysisWorker::prepare (double sampleRate)
{
    if (sampleRate > 0.0)
        sampleRateHz.store (sampleRate, std::memory_order_relaxed);
}

void AudioAnalysisWorker::setCapturing (bool shouldCapture) noexcept
{
    capturing.store (shouldCapture, std::memory_order_release);
}

bool AudioAnalysisWorker::isCapturing() const noexcept
{
    return capturing.load (std::memory_order_acquire);
}

void AudioAnalysisWorker::pushAudio (const float* samples, int count) noexcept
{
    if (! isCapturing() || samples == nullptr || count <= 0)
        return;

    // 1) Lock-free live waveform ring (256 samples) for the editor preview.
    //    Single producer (audio thread), single consumer (UI thread).
    {
        int wp = liveWritePos.load (std::memory_order_relaxed);
        for (int i = 0; i < count; ++i)
        {
            liveWave[static_cast<size_t> (wp)] = samples[i];
            wp = (wp + 1) % kLiveWaveformSize;
        }
        liveWritePos.store (wp, std::memory_order_release);

        int fill = liveFillCount.load (std::memory_order_relaxed);
        fill = (fill + count < kLiveWaveformSize) ? (fill + count) : kLiveWaveformSize;
        liveFillCount.store (fill, std::memory_order_release);
    }

    // 2) FIFO handoff to the analysis worker thread.
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (count, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i)
        queue[static_cast<size_t> (start1 + i)] = samples[i];
    for (int i = 0; i < size2; ++i)
        queue[static_cast<size_t> (start2 + i)] = samples[size1 + i];
    fifo.finishedWrite (size1 + size2);
}

void AudioAnalysisWorker::reset() noexcept
{
    // Disarm capture first so the audio thread stops touching the live ring
    // and the FIFO. Then publish clean scalar results immediately for the UI.
    capturing.store (false, std::memory_order_release);

    liveWave.fill (0.0f);
    liveWritePos.store (0, std::memory_order_relaxed);
    liveFillCount.store (0, std::memory_order_relaxed);

    frequencyHz.store (0.0, std::memory_order_relaxed);
    confidence.store (0.0f, std::memory_order_relaxed);
    tonic.store (0, std::memory_order_relaxed);
    minor.store (false, std::memory_order_relaxed);

    // Ask the worker thread to clear its own (non-atomic) state at the top of
    // its next iteration - no cross-thread mutation of worker-owned buffers.
    resetRequested.store (true, std::memory_order_release);
}

double AudioAnalysisWorker::latestFrequencyHz() const noexcept
{
    return frequencyHz.load (std::memory_order_relaxed);
}

float AudioAnalysisWorker::latestConfidence() const noexcept
{
    return confidence.load (std::memory_order_relaxed);
}

int AudioAnalysisWorker::analyzedWindowCount() const noexcept
{
    return windowCount.load (std::memory_order_relaxed);
}

int AudioAnalysisWorker::detectedTonic() const noexcept
{
    return tonic.load (std::memory_order_relaxed);
}

bool AudioAnalysisWorker::detectedMinor() const noexcept
{
    return minor.load (std::memory_order_relaxed);
}

void AudioAnalysisWorker::copyLiveWaveform (std::array<float, kLiveWaveformSize>& dest) const noexcept
{
    const int wp   = liveWritePos.load  (std::memory_order_acquire);
    const int fill = liveFillCount.load (std::memory_order_acquire);

    dest.fill (0.0f);
    const int count = (fill < kLiveWaveformSize) ? fill : kLiveWaveformSize;
    if (count <= 0)
        return;

    // The 'count' most recent samples, oldest first, left aligned.
    // Most recent sample lives at (wp - 1); oldest visible at (wp - count).
    for (int i = 0; i < count; ++i)
    {
        int idx = wp - count + i;
        idx = ((idx % kLiveWaveformSize) + kLiveWaveformSize) % kLiveWaveformSize;
        dest[static_cast<size_t> (i)] = liveWave[static_cast<size_t> (idx)];
    }
}

void AudioAnalysisWorker::handleResetRequest() noexcept
{
    // Drain anything still queued.
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (queueCapacity, start1, size1, start2, size2);
    fifo.finishedRead (size1 + size2);

    std::fill (window.begin(),        window.end(),        0.0f);
    std::fill (analysisFrame.begin(), analysisFrame.end(), 0.0f);
    keyEstimator.clear();

    writePosition = 0;
    framesSinceKeyUpdate = 0;
    windowCount.store (0, std::memory_order_relaxed);
}

void AudioAnalysisWorker::updateKeyEstimate (double freqHz, float conf) noexcept
{
    if (freqHz <= 0.0 || conf <= 0.0f)
        return;

    const int midi = static_cast<int> (std::llround (69.0 + 12.0 * std::log2 (freqHz / 440.0)));
    const int pc   = ((midi % 12) + 12) % 12;
    keyEstimator.addPitchClass (pc, static_cast<double> (conf));

    if (++framesSinceKeyUpdate >= keyRecomputeInterval)
    {
        framesSinceKeyUpdate = 0;
        const auto k = keyEstimator.estimate();
        if (k.valid)
        {
            tonic.store (k.tonic, std::memory_order_relaxed);
            minor.store (k.minor, std::memory_order_relaxed);
        }
    }
}

void AudioAnalysisWorker::run()
{
    int filled = 0;
    int samplesSinceAnalysis = 0;

    while (! threadShouldExit())
    {
        if (resetRequested.load (std::memory_order_acquire))
        {
            resetRequested.store (false, std::memory_order_release);
            handleResetRequest();
            filled = 0;
            samplesSinceAnalysis = 0;
        }

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead (static_cast<int> (scratch.size()),
                            start1, size1, start2, size2);
        const int available = size1 + size2;

        if (available == 0)
        {
            wait (8);
            continue;
        }

        int copied = 0;
        for (int i = 0; i < size1; ++i)
            scratch[static_cast<size_t> (copied++)] = queue[static_cast<size_t> (start1 + i)];
        for (int i = 0; i < size2; ++i)
            scratch[static_cast<size_t> (copied++)] = queue[static_cast<size_t> (start2 + i)];
        fifo.finishedRead (available);

        for (int i = 0; i < copied; ++i)
        {
            window[static_cast<size_t> (writePosition)] = scratch[static_cast<size_t> (i)];
            writePosition = (writePosition + 1) % analysisWindowSize;
            if (filled < analysisWindowSize)
                ++filled;

            ++samplesSinceAnalysis;
            if (filled == analysisWindowSize && samplesSinceAnalysis >= analysisHopSize)
            {
                for (int frameIndex = 0; frameIndex < analysisWindowSize; ++frameIndex)
                    analysisFrame[static_cast<size_t> (frameIndex)] =
                        window[static_cast<size_t> ((writePosition + frameIndex) % analysisWindowSize)];

                const auto estimate = pitchDetector.detect (
                    analysisFrame.data(), analysisWindowSize,
                    sampleRateHz.load (std::memory_order_relaxed));
                frequencyHz.store (estimate.frequencyHz, std::memory_order_relaxed);
                confidence.store (estimate.confidence, std::memory_order_relaxed);
                windowCount.fetch_add (1, std::memory_order_relaxed);
                samplesSinceAnalysis = 0;

                // Feed confident estimates into the key estimator.
                if (estimate.isValid())
                    updateKeyEstimate (estimate.frequencyHz, estimate.confidence);
            }
        }
    }
}
}
