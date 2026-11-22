#include "ArrangementEngine.h"
#include <algorithm>

namespace magicalchords
{
std::vector<MidiNoteEvent> ArrangementEngine::generate (
    const std::vector<Chord>& chords, ArrangementPart part, ArrangementStyle style) const
{
    std::vector<MidiNoteEvent> notes;
    if (chords.empty()) return notes;

    const int barsPerChord = 1;
    for (size_t bar = 0; bar < chords.size(); ++bar)
    {
        const auto& chord = chords[bar];
        const double barStart = static_cast<double>(bar * barsPerChord) * 4.0;
        const auto& tones = chord.midiNotes;
        if (tones.empty()) continue;

        switch (part)
        {
            case ArrangementPart::bass:
            {
                const int root = chord.rootMidi - 24;
                const int fifth = root + 7;
                const double beat = style == ArrangementStyle::edm ? 0.5 : 1.0;
                for (int i = 0; i < 4; ++i)
                    notes.push_back ({ (style == ArrangementStyle::hipHop && i == 2) ? fifth : root,
                                       barStart + i * beat, beat * 0.85, 100, 1 });
                break;
            }
            case ArrangementPart::lead:
            {
                const int sequence[] { 0, 1, 2, 1 };
                for (int i = 0; i < 4; ++i)
                {
                    const int index = sequence[i] % static_cast<int>(tones.size());
                    notes.push_back ({ tones[static_cast<size_t>(index)] + 12,
                                       barStart + i, 0.8, 82, 1 });
                }
                break;
            }
            case ArrangementPart::arpeggio:
            {
                const double step = style == ArrangementStyle::edm ? 0.5 : 1.0;
                for (int i = 0; i < static_cast<int>(4.0 / step); ++i)
                {
                    const int index = i % static_cast<int>(tones.size());
                    notes.push_back ({ tones[static_cast<size_t>(index)] + 12,
                                       barStart + i * step, step * 0.8, 76, 1 });
                }
                break;
            }
            case ArrangementPart::drums:
            {
                const int kick = 36, snare = 38, closedHat = 42;
                for (int beat = 0; beat < 4; ++beat)
                {
                    if (beat == 0 || (style == ArrangementStyle::edm && beat == 2))
                        notes.push_back ({ kick, barStart + beat, 0.2, 110, 10 });
                    if (beat == 1 || beat == 3)
                        notes.push_back ({ snare, barStart + beat, 0.2, 96, 10 });
                    if (style != ArrangementStyle::lofi || beat % 2 == 0)
                        notes.push_back ({ closedHat, barStart + beat, 0.12, 68, 10 });
                }
                break;
            }
        }
    }
    return notes;
}
}
