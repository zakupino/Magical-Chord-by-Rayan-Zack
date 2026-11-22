// Standalone validation harness for the pure-C++ DSP cores used by
// AudioAnalysisWorker: PitchDetector (YIN) and KeyEstimator (Krumhansl).
// No JUCE dependency - compiles the real production sources directly.
//
// Build:
//   g++ -std=c++17 -O2 -I/home/z/my-project/download/MagicalChords/Source \
//     -o /home/z/my-project/scripts/test_dsp_cores \
//     /home/z/my-project/scripts/test_dsp_cores.cpp \
//     /home/z/my-project/download/MagicalChords/Source/Analysis/PitchDetector.cpp

#include "Analysis/PitchDetector.h"
#include "Analysis/KeyEstimator.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> makeSine (double freq, double seconds, double sampleRate, double amp)
{
    const size_t total = static_cast<size_t> (seconds * sampleRate);
    std::vector<float> out (total);
    for (size_t i = 0; i < total; ++i)
        out[i] = static_cast<float> (amp * std::sin (2.0 * kPi * freq * static_cast<double> (i) / sampleRate));
    return out;
}

// midiToPitchClass: 0..11
int pcOf (int midi) { return ((midi % 12) + 12) % 12; }

// MIDI note number for a frequency (A4=440=69).
int midiOf (double hz)
{
    return static_cast<int> (std::llround (69.0 + 12.0 * std::log2 (hz / 440.0)));
}

int report (const char* name, bool ok)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    return ok ? 0 : 1;
}

} // namespace

int main()
{
    const double sr = 44100.0;
    int failures = 0;

    std::printf ("=== PitchDetector (YIN) validation ===\n");

    // Test 1: steady 220 Hz (A3) sine.
    {
        auto signal = makeSine (220.0, 0.2, sr, 0.4); // ~8820 samples
        magicalchords::PitchDetector det;
        const auto est = det.detect (signal.data(), static_cast<int> (signal.size()), sr);

        const bool freqOk = est.isValid() && std::abs (est.frequencyHz - 220.0) < 3.0;
        const bool confOk = est.isValid() && est.confidence > 0.5;
        std::printf ("Test 1 - 220 Hz sine\n");
        std::printf ("  freq = %.2f Hz (expected ~220), conf = %.3f\n",
                     est.frequencyHz, est.confidence);
        failures += report ("detected frequency within 3 Hz of 220", freqOk);
        failures += report ("confidence > 0.5", confOk);
    }

    // Test 2: 440 Hz (A4).
    {
        auto signal = makeSine (440.0, 0.2, sr, 0.4);
        magicalchords::PitchDetector det;
        const auto est = det.detect (signal.data(), static_cast<int> (signal.size()), sr);
        const bool ok = est.isValid() && std::abs (est.frequencyHz - 440.0) < 4.0;
        std::printf ("Test 2 - 440 Hz sine\n  freq = %.2f Hz, conf = %.3f\n",
                     est.frequencyHz, est.confidence);
        failures += report ("detected frequency within 4 Hz of 440", ok);
    }

    // Test 3: silence -> invalid estimate.
    {
        std::vector<float> silence (8192, 0.0f);
        magicalchords::PitchDetector det;
        const auto est = det.detect (silence.data(), static_cast<int> (silence.size()), sr);
        const bool ok = ! est.isValid();
        std::printf ("Test 3 - silence\n  valid = %d (expected 0)\n", est.isValid() ? 1 : 0);
        failures += report ("silence yields invalid estimate", ok);
    }

    std::printf ("\n=== KeyEstimator (Krumhansl-Schmuckler) validation ===\n");

    // Test 4: C-major scale pitch classes -> tonic C (0), major.
    {
        magicalchords::KeyEstimator ke;
        const int cmaj[] { 60, 62, 64, 65, 67, 69, 71, 72 }; // C D E F G A B C
        for (int m : cmaj)
            ke.addPitchClass (pcOf (m), 0.8);
        // Emphasise the tonic a little more (scales usually start/end on tonic).
        ke.addPitchClass (pcOf (60), 0.8);
        ke.addPitchClass (pcOf (72), 0.8);

        const auto k = ke.estimate();
        std::printf ("Test 4 - C-major scale\n  tonic = %d (expected 0), minor = %d (expected 0)\n",
                     k.tonic, k.minor ? 1 : 0);
        failures += report ("tonic == C (0)", k.valid && k.tonic == 0);
        failures += report ("scale is major",  k.valid && ! k.minor);
    }

    // Test 5: A-minor scale pitch classes -> tonic A (9), minor.
    {
        magicalchords::KeyEstimator ke;
        const int amin[] { 57, 59, 60, 62, 64, 65, 67, 69 }; // A B C D E F G A
        for (int m : amin)
            ke.addPitchClass (pcOf (m), 0.8);
        ke.addPitchClass (pcOf (57), 0.8);
        ke.addPitchClass (pcOf (69), 0.8);

        const auto k = ke.estimate();
        std::printf ("Test 5 - A-minor scale\n  tonic = %d (expected 9), minor = %d (expected 1)\n",
                     k.tonic, k.minor ? 1 : 0);
        failures += report ("tonic == A (9)", k.valid && k.tonic == 9);
        failures += report ("scale is minor", k.valid && k.minor);
    }

    // Test 6: insufficient data -> not valid.
    {
        magicalchords::KeyEstimator ke;
        ke.addPitchClass (0, 0.3); // total weight < 2.0
        const auto k = ke.estimate();
        std::printf ("Test 6 - insufficient data\n  valid = %d (expected 0)\n", k.valid ? 1 : 0);
        failures += report ("insufficient data => invalid", ! k.valid);
    }

    // Test 7: clear() resets the histogram.
    {
        magicalchords::KeyEstimator ke;
        for (int m : { 60, 64, 67 })
            ke.addPitchClass (pcOf (m), 1.0);
        ke.clear();
        const bool ok = ke.totalWeight() == 0.0;
        std::printf ("Test 7 - clear()\n  total weight after clear = %.3f (expected 0)\n",
                     ke.totalWeight());
        failures += report ("clear zeroes histogram", ok);
    }

    std::printf ("\n=== PitchDetector + KeyEstimator end-to-end ===\n");

    // Test 8: feed detected pitches from a C-major scale through PitchDetector
    // into KeyEstimator, exactly as the worker will. Confirms tonic == C.
    {
        magicalchords::PitchDetector det;
        magicalchords::KeyEstimator ke;
        const double cmajFreqs[] { 261.63, 293.66, 329.63, 349.23,
                                   392.00, 440.00, 493.88, 523.25 };
        int confidentFrames = 0;
        for (double f : cmajFreqs)
        {
            auto sig = makeSine (f, 0.18, sr, 0.4);
            const auto est = det.detect (sig.data(), static_cast<int> (sig.size()), sr);
            if (est.isValid())
            {
                ++confidentFrames;
                ke.addPitchClass (pcOf (midiOf (est.frequencyHz)), est.confidence);
            }
        }
        const auto k = ke.estimate();
        std::printf ("Test 8 - end-to-end C-major (confident frames = %d)\n", confidentFrames);
        std::printf ("  tonic = %d (expected 0), minor = %d (expected 0)\n",
                     k.tonic, k.minor ? 1 : 0);
        failures += report ("end-to-end tonic == C", k.valid && k.tonic == 0);
        failures += report ("end-to-end major",      k.valid && ! k.minor);
        failures += report ("at least 6 confident frames", confidentFrames >= 6);
    }

    std::printf ("\n=== %s (%d failure%s) ===\n",
                 failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
