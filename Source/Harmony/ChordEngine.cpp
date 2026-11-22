#include "ChordEngine.h"

#include <algorithm>
#include <array>

namespace magicalchords
{
namespace
{
constexpr std::array<int, 12> majorScale { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19 };
constexpr std::array<int, 12> minorScale { 0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19 };

struct Degree
{
    int number;
    int third;
    int fifth;
    int seventh;
};

Degree getDegree (int degree, ScaleType scale)
{
    // Diatonic seventh-chord qualities for major and natural-minor scales.
    static constexpr Degree major[] {
        { 1, 4, 7, 11 }, { 2, 3, 7, 10 }, { 3, 3, 6, 10 },
        { 4, 4, 7, 11 }, { 5, 4, 7, 10 }, { 6, 3, 7, 10 },
        { 7, 3, 6, 9 }
    };
    static constexpr Degree minor[] {
        { 1, 3, 7, 10 }, { 2, 3, 6, 9 }, { 3, 4, 7, 11 },
        { 4, 3, 7, 10 }, { 5, 3, 7, 10 }, { 6, 4, 7, 11 },
        { 7, 4, 7, 10 }
    };
    return scale == ScaleType::major ? major[degree - 1] : minor[degree - 1];
}
}

std::vector<Chord> ChordEngine::generate (int tonic, ScaleType scale,
                                          ProgressionPreset preset, int octave) const
{
    tonic = ((tonic % 12) + 12) % 12;
    octave = std::clamp (octave, 1, 6);
    const int baseMidi = 12 * (octave + 1) + tonic;
    const auto& intervals = scale == ScaleType::major ? majorScale : minorScale;

    std::array<int, 4> degrees { 1, 5, 6, 4 };
    if (scale == ScaleType::naturalMinor)
        degrees = { 1, 6, 3, 7 };
    if (preset == ProgressionPreset::cinematic)
        degrees = scale == ScaleType::major
            ? std::array<int, 4> { 1, 3, 6, 4 }
            : std::array<int, 4> { 1, 4, 6, 5 };

    std::vector<Chord> result;
    result.reserve (degrees.size());

    for (const int degree : degrees)
    {
        const int scaleIndex = degree - 1;
        const auto quality = getDegree (degree, scale);
        const int root = baseMidi + intervals[static_cast<size_t> (scaleIndex)];
        Chord chord;
        chord.degree = degree;
        chord.rootMidi = root;
        chord.name = noteName (root);
        if (quality.third == 4)
            chord.name += quality.seventh == 11 ? "maj7" : "7";
        else
            chord.name += quality.fifth == 6 ? "m7b5" : "m7";
        chord.midiNotes = { root, root + quality.third,
                            root + quality.fifth, root + quality.seventh };
        result.push_back (std::move (chord));
    }
    return result;
}

std::string ChordEngine::noteName (int midiNote)
{
    static constexpr const char* names[] {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    return names[((midiNote % 12) + 12) % 12];
}
}
