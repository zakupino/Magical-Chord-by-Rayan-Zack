#pragma once

#include <array>
#include <string>
#include <vector>

namespace magicalchords
{
enum class ScaleType { major, naturalMinor };
enum class ProgressionPreset { pop, emotionalMinor, cinematic };

struct Chord
{
    int degree = 1;
    int rootMidi = 60;
    std::string name;
    std::vector<int> midiNotes;
    int durationBeats = 4;
};

class ChordEngine
{
public:
    // tonic is a chromatic pitch class: C=0, C#=1, ... B=11.
    std::vector<Chord> generate (int tonic,
                                 ScaleType scale,
                                 ProgressionPreset preset,
                                 int octave = 3) const;

    static std::string noteName (int midiNote);
};
}
