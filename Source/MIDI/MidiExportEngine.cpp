#include "MidiExportEngine.h"

#include <cmath>
#include <vector>

namespace magicalchords
{
bool MidiExportEngine::exportChords (const std::vector<Chord>& chords,
                                     const juce::File& destination,
                                     double tempoBpm)
{
    if (chords.empty() || ! std::isfinite (tempoBpm) || tempoBpm < 20.0 || tempoBpm > 300.0)
        return false;

    constexpr int ticksPerQuarter = 960;
    constexpr double beatsPerBar = 4.0;
    const double secondsPerBeat = 60.0 / tempoBpm;
    const double secondsPerBar = beatsPerBar * secondsPerBeat;
    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::tempoMetaEvent (
        static_cast<int> (60000000.0 / tempoBpm)), 0.0);

    for (size_t bar = 0; bar < chords.size(); ++bar)
    {
        const double start = static_cast<double> (bar) * secondsPerBar;
        const double end = start + secondsPerBar - 0.001;
        for (const int note : chords[bar].midiNotes)
        {
            const int safeNote = juce::jlimit (0, 127, note);
            sequence.addEvent (juce::MidiMessage::noteOn (1, safeNote, static_cast<juce::uint8> (88)), start);
            sequence.addEvent (juce::MidiMessage::noteOff (1, safeNote), end);
        }
    }

    sequence.updateMatchedPairs();
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticksPerQuarter);
    midi.addTrack (sequence);

    if (destination.existsAsFile() && ! destination.deleteFile())
        return false;
    juce::FileOutputStream stream (destination);
    if (! stream.openedOk())
        return false;
    midi.writeTo (stream);
    stream.flush();
    return ! stream.getStatus().failed();
}

bool MidiExportEngine::exportPart (const std::vector<MidiNoteEvent>& notes,
                                   const juce::File& destination,
                                   double tempoBpm)
{
    if (notes.empty() || ! std::isfinite (tempoBpm) || tempoBpm < 20.0 || tempoBpm > 300.0)
        return false;

    const double secondsPerBeat = 60.0 / tempoBpm;
    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::tempoMetaEvent (
        static_cast<int> (60000000.0 / tempoBpm)), 0.0);

    for (const auto& note : notes)
    {
        if (note.startBeat < 0.0 || note.durationBeats <= 0.0)
            continue;
        const double start = note.startBeat * secondsPerBeat;
        const double end = (note.startBeat + note.durationBeats) * secondsPerBeat;
        const int channel = juce::jlimit (1, 16, note.channel);
        const int pitch = juce::jlimit (0, 127, note.note);
        const auto velocity = static_cast<juce::uint8> (juce::jlimit (1, 127, note.velocity));
        sequence.addEvent (juce::MidiMessage::noteOn (channel, pitch, velocity), start);
        sequence.addEvent (juce::MidiMessage::noteOff (channel, pitch), end);
    }
    sequence.updateMatchedPairs();

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (960);
    midi.addTrack (sequence);
    if (destination.existsAsFile() && ! destination.deleteFile())
        return false;
    juce::FileOutputStream stream (destination);
    if (! stream.openedOk())
        return false;
    midi.writeTo (stream);
    stream.flush();
    return ! stream.getStatus().failed();
}
}
