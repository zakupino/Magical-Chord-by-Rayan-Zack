#pragma once

// =============================================================================
//  Magical Chords - KeyEstimator
//  Pure-C++ (no JUCE dependency), header-only, unit-testable.
//
//  Accumulates a weighted pitch-class histogram from confident pitch estimates
//  and estimates the musical key (tonic pitch class 0..11 + major/minor) using
//  the Krumhansl-Schmuckler key-finding algorithm: Pearson correlation of the
//  histogram against the major / minor probe profiles over all 12 rotations.
//
//  Thread model: not thread-safe. The AudioAnalysisWorker calls addPitchClass()
//  and estimate() exclusively from its worker thread; the published tonic/minor
//  result is then copied through std::atomic in the worker for the UI thread.
// =============================================================================

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace magicalchords
{

struct KeyEstimate
{
    int  tonic = 0;     // chromatic pitch class 0..11 (C=0 ... B=11)
    bool minor = false; // true => natural minor, false => major
    bool valid = false; // false until enough weighted evidence has accumulated
};

class KeyEstimator
{
public:
    // Add one observation. pc is a chromatic pitch class 0..11 (caller maps
    // MIDI note -> pc). weight is typically the pitch confidence in 0..1.
    void addPitchClass (int pc, double weight) noexcept
    {
        if (weight <= 0.0)
            return;
        const int idx = ((pc % 12) + 12) % 12;
        histogram_[static_cast<size_t> (idx)] += weight;
    }

    void clear() noexcept { histogram_.fill (0.0); }

    double totalWeight() const noexcept
    {
        double s = 0.0;
        for (double v : histogram_)
            s += v;
        return s;
    }

    // Returns the best-scoring key, or {.,.,valid=false} if insufficient data.
    KeyEstimate estimate() const noexcept
    {
        KeyEstimate result;
        const double hSum = totalWeight();
        if (hSum < 2.0)
            return result; // not enough confident observations yet

        const double hMean = hSum / 12.0;
        double hVar = 0.0;
        for (double v : histogram_)
            hVar += (v - hMean) * (v - hMean);
        const double hStd = std::sqrt (hVar / 12.0);
        if (hStd < 1.0e-9)
            return result;

        const auto maj = bestRotation (kMajorProfile, hMean, hStd);
        const auto min = bestRotation (kMinorProfile, hMean, hStd);

        result.valid = true;
        if (maj.first >= min.first)
        {
            result.tonic = maj.second;
            result.minor = false;
        }
        else
        {
            result.tonic = min.second;
            result.minor = true;
        }
        return result;
    }

private:
    using ScoreShift = std::pair<double, int>;

    ScoreShift bestRotation (const std::array<double, 12>& profile,
                             double hMean, double hStd) const noexcept
    {
        const auto [pMean, pStd] = profileMeanStd (profile);
        if (pStd < 1.0e-9)
            return { -1.0e300, 0 };

        double bestR = -1.0e300;
        int    bestShift = 0;
        for (int shift = 0; shift < 12; ++shift)
        {
            double num = 0.0;
            for (int i = 0; i < 12; ++i)
            {
                const double h = histogram_[static_cast<size_t> ((i + shift) % 12)];
                const double p = profile[static_cast<size_t> (i)];
                num += (h - hMean) * (p - pMean);
            }
            const double r = num / (12.0 * hStd * pStd);
            if (r > bestR)
            {
                bestR = r;
                bestShift = shift;
            }
        }
        return { bestR, bestShift };
    }

    static std::pair<double, double> profileMeanStd (const std::array<double, 12>& p) noexcept
    {
        double s = 0.0;
        for (double v : p) s += v;
        const double mean = s / 12.0;
        double v2 = 0.0;
        for (double v : p) v2 += (v - mean) * (v - mean);
        return { mean, std::sqrt (v2 / 12.0) };
    }

    // Krumhansl-Kessler probe profiles.
    static constexpr std::array<double, 12> kMajorProfile {
        6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88
    };
    static constexpr std::array<double, 12> kMinorProfile {
        6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17
    };

    std::array<double, 12> histogram_ {};
};

} // namespace magicalchords
