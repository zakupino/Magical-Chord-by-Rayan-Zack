#pragma once

#include "../Harmony/ChordEngine.h"
#include <vector>

namespace magicalchords
{
enum class ArrangementPart { bass, lead, arpeggio, drums };
enum class ArrangementStyle { pop, edm, cinematic, lofi, hipHop };

struct MidiNoteEvent
{
    int note = 60;
    double startBeat = 0.0;
    double durationBeats = 1.0;
    int velocity = 90;
    int channel = 1;
};

class ArrangementEngine
{
public:
    // Deterministic, rule-based starter arrangements. These are not AI inference.
    std::vector<MidiNoteEvent> generate (const std::vector<Chord>& chords,
                                        ArrangementPart part,
                                        ArrangementStyle style = ArrangementStyle::pop) const;
};
}
