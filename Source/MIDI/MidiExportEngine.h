#pragma once

#include "../Harmony/ChordEngine.h"
#include "../Arrangement/ArrangementEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace magicalchords
{
class MidiExportEngine
{
public:
    // Writes one chord per bar to a standard MIDI file (960 PPQ).
    static bool exportChords (const std::vector<Chord>& chords,
                              const juce::File& destination,
                              double tempoBpm = 120.0);
    static bool exportPart (const std::vector<MidiNoteEvent>& notes,
                            const juce::File& destination,
                            double tempoBpm = 120.0);
};
}
