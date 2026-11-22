#include "PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace magicalchords
{
PitchEstimate PitchDetector::detect (const float* samples,
                                     int numSamples,
                                     double sampleRate,
                                     double minFrequencyHz,
                                     double maxFrequencyHz,
                                     float threshold) const noexcept
{
    if (samples == nullptr || numSamples < 64 || sampleRate <= 0.0
        || minFrequencyHz <= 0.0 || maxFrequencyHz <= minFrequencyHz)
        return {};

    const int minLag = std::max (2, static_cast<int> (sampleRate / maxFrequencyHz));
    const int maxLag = std::min (numSamples / 2,
                                 static_cast<int> (sampleRate / minFrequencyHz));
    if (maxLag <= minLag + 2)
        return {};

    // Scratch storage is local to the analysis call. Run this off the audio thread.
    std::vector<double> difference (static_cast<size_t> (maxLag + 1), 0.0);
    std::vector<double> normalized (static_cast<size_t> (maxLag + 1), 1.0);

    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        const int count = numSamples - maxLag;
        for (int i = 0; i < count; ++i)
        {
            const double delta = static_cast<double> (samples[i])
                               - static_cast<double> (samples[i + lag]);
            sum += delta * delta;
        }
        difference[static_cast<size_t> (lag)] = sum;
    }

    double runningSum = 0.0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        runningSum += difference[static_cast<size_t> (lag)];
        normalized[static_cast<size_t> (lag)] =
            runningSum > 0.0
                ? difference[static_cast<size_t> (lag)] * lag / runningSum
                : 1.0;
    }

    int bestLag = -1;
    const double safeThreshold = std::clamp (static_cast<double> (threshold), 0.02, 0.5);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        if (normalized[static_cast<size_t> (lag)] < safeThreshold)
        {
            while (lag + 1 <= maxLag
                   && normalized[static_cast<size_t> (lag + 1)]
                        < normalized[static_cast<size_t> (lag)])
                ++lag;
            bestLag = lag;
            break;
        }
    }

    if (bestLag < 0)
        return {};

    // Parabolic interpolation around the integer peak for sub-sample lag.
    // Guard the neighbours: normalised[] is valid for indices 0..maxLag only.
    // YIN normalised values lie in [0,1] (0 = perfect periodicity, 1 = none),
    // so 1.0 is the safe "no information" sentinel for out-of-range neighbours.
    const double left   = (bestLag - 1 >= 0)      ? normalized[static_cast<size_t> (bestLag - 1)] : 1.0;
    const double centre = normalized[static_cast<size_t> (bestLag)];
    const double right  = (bestLag + 1 <= maxLag) ? normalized[static_cast<size_t> (bestLag + 1)] : 1.0;
    const double denominator = left - 2.0 * centre + right;
    const double adjustment = std::abs (denominator) > 1.0e-12
        ? 0.5 * (left - right) / denominator : 0.0;
    const double refinedLag = static_cast<double> (bestLag) + adjustment;
    const double frequency = sampleRate / refinedLag;
    const float confidence = static_cast<float> (std::clamp (1.0 - centre, 0.0, 1.0));

    if (! std::isfinite (frequency) || frequency < minFrequencyHz
        || frequency > maxFrequencyHz || confidence < 0.5f)
        return {};

    return { frequency, confidence };
}
}
