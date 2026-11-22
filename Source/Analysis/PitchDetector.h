#pragma once

namespace magicalchords
{
struct PitchEstimate
{
    double frequencyHz = 0.0;
    float confidence = 0.0f;

    bool isValid() const noexcept { return frequencyHz > 0.0 && confidence > 0.0f; }
};

// YIN-style monophonic pitch estimator. Scratch storage is allocated per call,\n// so invoke this only from a worker thread, never from the audio callback.
// Call from a worker thread, not from the real-time audio callback.
class PitchDetector
{
public:
    PitchEstimate detect (const float* samples,
                          int numSamples,
                          double sampleRate,
                          double minFrequencyHz = 65.0,
                          double maxFrequencyHz = 1000.0,
                          float threshold = 0.15f) const noexcept;
};
}
