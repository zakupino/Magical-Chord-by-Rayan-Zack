#include "PluginEditor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
    const juce::Colour colBg      { 0xff070b10 };
    const juce::Colour colPanel   { 0xff0d1520 };
    const juce::Colour colCard    { 0xff121d2b };
    const juce::Colour colElev    { 0xff192738 };
    const juce::Colour colBorder  { 0xff23364d };
    const juce::Colour colAccent  { 0xff22e5b5 };
    const juce::Colour colBlue    { 0xff1d8cf8 };
    const juce::Colour colPurple  { 0xff8b5cf6 };
    const juce::Colour colAmber   { 0xfff59e0b };
    const juce::Colour colPink    { 0xffec4899 };
    const juce::Colour colText    { 0xfff1f5f9 };
    const juce::Colour colMuted   { 0xff8fa3b8 };

    bool hasGeneratedFromSession = false;
    bool aiAutoPilotEnabled = true;

    constexpr int kWaveHistorySize = 512;
    std::array<float, kWaveHistorySize> recordedWave {};
    int recordedWaveCount = 0;
    int recordedWaveWritePos = 0;
    bool hasRecordedWave = false;
    int voicedTimerTicks = 0;
    int lastAutoTriggeredFrames = 0;
    double lastAutoTriggeredPitch = -1.0;

    // Semitone offsets (-12..+12) for Piano, Bass, Lead, Drums, Pluck
    std::array<int, 5> stemSemitoneShift { 0, 0, 0, 0, 0 };
    std::array<juce::Rectangle<int>, 5> semiBadgeBounds {};
    std::array<juce::Rectangle<int>, 5> pluckCardBounds {};
    juce::Rectangle<int> pluckDragBounds {};
    juce::Rectangle<int> aiAutoBadgeBounds {};
    juce::Rectangle<int> chordModeBadgeBounds {};
    int pluckVariation = 0;
    int chordComplexityMode = 1; // 0 = Basic Chords, 1 = Advanced Chords
    std::unique_ptr<MidiDragRelay> pluckDragRelay;

    struct VocalFrame
    {
        int midiNote = -1;
        float confidence = 0.0f;
        float level = 0.0f;
    };

    constexpr int kMaxVocalFrames = 512;
    std::array<VocalFrame, kMaxVocalFrames> vocalTrail {};
    int vocalTrailCount = 0;
    int vocalTrailWritePos = 0;

    void resetRecordedWaveform()
    {
        recordedWave.fill (0.0f);
        recordedWaveCount = 0;
        recordedWaveWritePos = 0;
        hasRecordedWave = false;
        voicedTimerTicks = 0;
        lastAutoTriggeredFrames = 0;
        lastAutoTriggeredPitch = -1.0;

        vocalTrail.fill ({});
        vocalTrailCount = 0;
        vocalTrailWritePos = 0;
    }

    int hzToMidiPitch (double hz)
    {
        if (hz <= 25.0 || hz > 4000.0)
            return -1;
        return juce::jlimit (24, 108, static_cast<int> (std::llround (69.0 + 12.0 * std::log2 (hz / 440.0))));
    }

    template <size_t N>
    void appendToRecordedWaveform (const std::array<float, N>& samples, double detectedHz, float conf)
    {
        if (N == 0)
            return;

        constexpr int buckets = 8;
        const size_t step = std::max<size_t> (1, N / buckets);
        float overallMax = 0.0f;
        const int midi = hzToMidiPitch (detectedHz);
        const bool hasActiveVoice = (midi > 0) || (conf > 0.04f);

        if (hasActiveVoice)
            ++voicedTimerTicks;

        for (int b = 0; b < buckets; ++b)
        {
            const size_t startIdx = static_cast<size_t> (b) * step;
            if (startIdx >= N)
                break;

            const size_t endIdx = std::min (N, startIdx + step);
            float maxAbs = 0.0f;
            float signedPeak = 0.0f;

            for (size_t i = startIdx; i < endIdx; ++i)
            {
                const float v = samples[i];
                const float a = std::abs (v);
                if (a > maxAbs)
                {
                    maxAbs = a;
                    signedPeak = v;
                }
            }

            overallMax = std::max (overallMax, maxAbs);

            float sampleToStore = 0.0f;
            if (maxAbs > 0.00002f)
            {
                sampleToStore = juce::jlimit (-1.0f, 1.0f, signedPeak * 6.0f);
            }
            else if (hasActiveVoice)
            {
                const float phase = static_cast<float> ((voicedTimerTicks * buckets + b) * 0.65);
                const float env = juce::jlimit (0.14f, 0.85f, std::max (conf, 0.28f) * (0.55f + 0.45f * std::sin (phase * 0.23f)));
                sampleToStore = std::sin (phase) * env;
            }

            if (std::abs (sampleToStore) > 0.0002f)
                hasRecordedWave = true;

            recordedWave[static_cast<size_t> (recordedWaveWritePos)] = sampleToStore;
            recordedWaveWritePos = (recordedWaveWritePos + 1) % kWaveHistorySize;
            recordedWaveCount = std::min (kWaveHistorySize, recordedWaveCount + 1);
        }

        if (midi > 0 && (overallMax > 0.00002f || conf > 0.03f))
        {
            vocalTrail[static_cast<size_t> (vocalTrailWritePos)] = { midi, std::max (conf, 0.35f), std::max (overallMax, 0.15f) };
            vocalTrailWritePos = (vocalTrailWritePos + 1) % kMaxVocalFrames;
            vocalTrailCount = std::min (kMaxVocalFrames, vocalTrailCount + 1);
        }
    }

    float getOrderedRecordedSample (int orderedIndex)
    {
        if (recordedWaveCount <= 0)
            return 0.0f;

        const int clampedIdx = juce::jlimit (0, recordedWaveCount - 1, orderedIndex);
        const int startIdx = (recordedWaveCount < kWaveHistorySize) ? 0 : recordedWaveWritePos;
        const int actualIdx = (startIdx + clampedIdx) % kWaveHistorySize;
        return recordedWave[static_cast<size_t> (actualIdx)];
    }

    VocalFrame getOrderedVocalFrame (int orderedIndex)
    {
        if (vocalTrailCount <= 0)
            return {};

        const int clampedIdx = juce::jlimit (0, vocalTrailCount - 1, orderedIndex);
        const int startIdx = (vocalTrailCount < kMaxVocalFrames) ? 0 : vocalTrailWritePos;
        const int actualIdx = (startIdx + clampedIdx) % kMaxVocalFrames;
        return vocalTrail[static_cast<size_t> (actualIdx)];
    }

    int getDynamicBarCount()
    {
        const int effectiveFrames = std::max (vocalTrailCount, voicedTimerTicks);
        if (effectiveFrames >= 540) return 64;
        if (effectiveFrames >= 270) return 32;
        if (effectiveFrames >= 135) return 16;
        if (effectiveFrames >= 65)  return 8;
        return 4;
    }

    struct VisualNote
    {
        double startBeat = 0.0;
        double lengthBeats = 1.0;
        int midiNote = 60;
        juce::uint8 velocity = 100;
    };

    std::vector<int> getScaleIntervals (int scaleIndex)
    {
        switch (scaleIndex)
        {
            case 1:  return { 0, 2, 3, 5, 7, 8, 10 };
            case 2:  return { 0, 2, 3, 5, 7, 9, 10 };
            case 3:  return { 0, 2, 4, 5, 7, 9, 10 };
            case 4:  return { 0, 1, 3, 5, 7, 8, 10 };
            case 5:  return { 0, 2, 4, 6, 7, 9, 11 };
            case 6:  return { 0, 1, 3, 5, 6, 8, 10 };
            default: return { 0, 2, 4, 5, 7, 9, 11 };
        }
    }

    const char* getNoteName (int semitone)
    {
        static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((semitone % 12) + 12) % 12];
    }

    juce::String getChordSymbol (int rootSemitone, int scaleIdx, int degree, int barSeed = 0)
    {
        const auto scale = getScaleIntervals (scaleIdx);
        const int sz = static_cast<int> (scale.size());
        const int deg = ((degree % sz) + sz) % sz;
        const int chordRoot = rootSemitone + scale[static_cast<size_t> (deg)] + stemSemitoneShift[0];

        const int thirdInterval = scale[static_cast<size_t> ((deg + 2) % sz)] - scale[static_cast<size_t> (deg)];
        const int normThird = ((thirdInterval % 12) + 12) % 12;

        const int fifthInterval = scale[static_cast<size_t> ((deg + 4) % sz)] - scale[static_cast<size_t> (deg)];
        const int normFifth = ((fifthInterval % 12) + 12) % 12;

        const juce::String name = getNoteName (chordRoot);
        if (normFifth == 6)
            return (chordComplexityMode == 0) ? (name + "dim") : (name + "m7b5");

        if (chordComplexityMode == 0)
            return (normThird == 3) ? (name + "m") : name;

        if (normThird == 3)
        {
            if (barSeed % 5 == 4) return name + "m11";
            if (barSeed % 3 == 2) return name + "m9";
            return name + "m7";
        }
        if (deg == 0 || scaleIdx == 5 || (scaleIdx == 0 && deg == 3))
        {
            if (barSeed % 4 == 3) return name + "maj9";
            if (barSeed % 3 == 2) return name + "add9";
            return name + "maj7";
        }
        if (deg == 4 || scaleIdx == 3)
        {
            if (barSeed % 3 == 2) return name + "13";
            return name + "9";
        }
        return (barSeed % 2 == 1) ? (name + "add9") : (name + "sus2");
    }

    juce::String freqToNoteLabel (double hz)
    {
        const int midiInt = hzToMidiPitch (hz);
        if (midiInt < 0)
            return "--";
        const int octave = (midiInt / 12) - 1;
        return juce::String (getNoteName (midiInt)) + juce::String (octave);
    }

    void computeVocalPhrasePlan (int tonic,
                                 int scaleIdx,
                                 int barCount,
                                 int variationSeed,
                                 double currentPitchHz,
                                 std::vector<int>& outBarDegrees,
                                 std::vector<int>& outStepMelodyDegs,
                                 float& outVocalDensity)
    {
        const auto scale = getScaleIntervals (scaleIdx);
        const int sz = static_cast<int> (scale.size());
        const int safeBars = std::max (4, barCount);

        outBarDegrees.assign (static_cast<size_t> (safeBars), 0);
        outStepMelodyDegs.assign (static_cast<size_t> (safeBars * 4), 0);

        std::array<float, 7> degreeWeights {};
        degreeWeights.fill (0.0f);

        int firstMidi = -1;
        int lastMidi = -1;
        int activeFrames = 0;

        for (int i = 0; i < vocalTrailCount; ++i)
        {
            const VocalFrame f = getOrderedVocalFrame (i);
            if (f.midiNote > 0)
            {
                if (firstMidi < 0)
                    firstMidi = f.midiNote;
                lastMidi = f.midiNote;
                ++activeFrames;

                const int pc = ((f.midiNote - tonic) % 12 + 12) % 12;
                int bestDeg = 0;
                int bestDist = 99;
                for (int d = 0; d < sz; ++d)
                {
                    const int dist = std::abs (((scale[static_cast<size_t> (d)] - pc) + 18) % 12 - 6);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        bestDeg = d;
                    }
                }
                degreeWeights[static_cast<size_t> (bestDeg)] += std::max (0.15f, f.confidence + f.level * 2.0f);
            }
        }

        if (activeFrames == 0 && hzToMidiPitch (currentPitchHz) > 0)
        {
            const int m = hzToMidiPitch (currentPitchHz);
            const int pc = ((m - tonic) % 12 + 12) % 12;
            int bestDeg = 0;
            int bestDist = 99;
            for (int d = 0; d < sz; ++d)
            {
                const int dist = std::abs (((scale[static_cast<size_t> (d)] - pc) + 18) % 12 - 6);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestDeg = d;
                }
            }
            degreeWeights[static_cast<size_t> (bestDeg)] = 1.0f;
            firstMidi = m;
            lastMidi = m;
            activeFrames = 1;
        }

        outVocalDensity = juce::jlimit (0.0f, 1.0f, static_cast<float> (activeFrames) / 48.0f);

        static const int templates[6][4] {
            { 0, 4, 5, 3 },
            { 5, 3, 0, 4 },
            { 0, 2, 3, 4 },
            { 1, 4, 0, 5 },
            { 3, 4, 2, 5 },
            { 0, 5, 3, 4 }
        };

        const int tplIdx = ((variationSeed % 6) + 6) % 6;
        const int melodicDelta = (firstMidi > 0 && lastMidi > 0) ? (lastMidi - firstMidi) : 0;
        const int vocalShift = (activeFrames > 0) ? (std::abs (melodicDelta) % sz) : ((variationSeed / 6) % sz);

        int base4[4] {
            templates[tplIdx][0] % sz,
            templates[tplIdx][1] % sz,
            templates[tplIdx][2] % sz,
            templates[tplIdx][3] % sz
        };

        if (activeFrames > 0)
        {
            int topDeg = 0;
            float topW = -1.0f;
            for (int d = 0; d < sz; ++d)
            {
                if (degreeWeights[static_cast<size_t> (d)] > topW)
                {
                    topW = degreeWeights[static_cast<size_t> (d)];
                    topDeg = d;
                }
            }
            if (topDeg != 0 && (variationSeed % 3 != 0))
                base4[0] = topDeg % sz;
        }

        for (int bar = 0; bar < safeBars; ++bar)
        {
            const int phraseIdx = bar / 4;
            const int inPhraseBar = bar % 4;

            int deg = base4[static_cast<size_t> (inPhraseBar)];
            if (vocalTrailCount > 0 && inPhraseBar != 0)
            {
                const int trailIdx = juce::jlimit (0, vocalTrailCount - 1, (bar * vocalTrailCount) / safeBars);
                const VocalFrame vf = getOrderedVocalFrame (trailIdx);
                if (vf.midiNote > 0)
                {
                    const int pc = ((vf.midiNote - tonic) % 12 + 12) % 12;
                    int bestDeg = deg;
                    int bestDist = 99;
                    for (int d = 0; d < sz; ++d)
                    {
                        const int dist = std::abs (((scale[static_cast<size_t> (d)] - pc) + 18) % 12 - 6);
                        if (dist < bestDist)
                        {
                            bestDist = dist;
                            bestDeg = d;
                        }
                    }
                    deg = bestDeg;
                }
            }

            if (phraseIdx > 0 && inPhraseBar == 3)
                deg = ((phraseIdx % 2 == 1) ? (4 % sz) : 0);

            outBarDegrees[static_cast<size_t> (bar)] = ((deg + ((phraseIdx > 0) ? vocalShift : 0)) % sz);
        }

        const int totalSteps = safeBars * 4;
        for (int step = 0; step < totalSteps; ++step)
        {
            const int barIdx = juce::jlimit (0, safeBars - 1, step / 4);
            const int chordDeg = outBarDegrees[static_cast<size_t> (barIdx)];

            if (vocalTrailCount > 0)
            {
                const int trailIdx = juce::jlimit (0, vocalTrailCount - 1, (step * vocalTrailCount) / totalSteps);
                const VocalFrame vf = getOrderedVocalFrame (trailIdx);
                if (vf.midiNote > 0)
                {
                    const int pc = ((vf.midiNote - tonic) % 12 + 12) % 12;
                    int nearest = chordDeg;
                    int bestDiff = 99;
                    for (int d = 0; d < sz; ++d)
                    {
                        const int diff = std::abs (((scale[static_cast<size_t> (d)] - pc) + 18) % 12 - 6);
                        if (diff < bestDiff)
                        {
                            bestDiff = diff;
                            nearest = d;
                        }
                    }
                    outStepMelodyDegs[static_cast<size_t> (step)] = (step % 4 == 0) ? chordDeg : nearest;
                    continue;
                }
            }

            const int arpSteps[4] { 0, 2, 4, 2 };
            outStepMelodyDegs[static_cast<size_t> (step)] = (chordDeg + arpSteps[(step + variationSeed) % 4]) % sz;
        }
    }

    std::vector<VisualNote> extractNotesFromSequence (const juce::MidiMessageSequence& seq, double ticksPerQuarter)
    {
        std::vector<VisualNote> notes;
        struct ActiveNote
        {
            double startBeat = 0.0;
            juce::uint8 velocity = 100;
            bool active = false;
        };

        std::array<ActiveNote, 128> activeTable {};

        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto* holder = seq.getEventPointer (i);
            if (holder == nullptr)
                continue;

            const auto& msg = holder->message;
            const double beatPos = msg.getTimeStamp() / ticksPerQuarter;

            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                const int noteNum = juce::jlimit (0, 127, msg.getNoteNumber());
                activeTable[static_cast<size_t> (noteNum)] = { beatPos, msg.getVelocity(), true };
            }
            else if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
            {
                const int noteNum = juce::jlimit (0, 127, msg.getNoteNumber());
                auto& act = activeTable[static_cast<size_t> (noteNum)];
                if (act.active)
                {
                    const double len = std::max (0.125, beatPos - act.startBeat);
                    notes.push_back ({ act.startBeat, len, noteNum, act.velocity });
                    act.active = false;
                }
            }
        }

        for (int n = 0; n < 128; ++n)
        {
            auto& act = activeTable[static_cast<size_t> (n)];
            if (act.active)
                notes.push_back ({ act.startBeat, 1.0, n, act.velocity });
        }

        return notes;
    }
}

MidiDragRelay::MidiDragRelay (MagicalChordsAudioProcessorEditor& editorIn,
                               int cardIndexIn,
                               juce::Component& targetCompIn)
    : editor (editorIn), cardIndex (cardIndexIn), targetComp (targetCompIn)
{
}

void MidiDragRelay::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;

    if (aiAutoBadgeBounds.contains (e.getPosition()))
    {
        aiAutoPilotEnabled = ! aiAutoPilotEnabled;
        if (aiAutoPilotEnabled)
        {
            hasGeneratedFromSession = true;
            for (int k = 0; k < 5; ++k)
                editor.buildMidiFile (k);
        }
        targetComp.repaint();
        return;
    }

    if (chordModeBadgeBounds.contains (e.getPosition()))
    {
        chordComplexityMode = (chordComplexityMode == 0) ? 1 : 0;
        hasGeneratedFromSession = true;
        for (int k = 0; k < 5; ++k)
            editor.buildMidiFile (k);
        targetComp.repaint();
        return;
    }

    for (size_t i = 0; i < semiBadgeBounds.size(); ++i)
    {
        if (semiBadgeBounds[i].contains (e.getPosition()))
        {
            const int delta = e.mods.isPopupMenu() ? -1 : 1;
            stemSemitoneShift[i] = juce::jlimit (-12, 12, stemSemitoneShift[i] + delta);
            hasGeneratedFromSession = true;
            editor.buildMidiFile (static_cast<int> (i));
            if (i == 0)
            {
                for (int k = 0; k < 5; ++k)
                    editor.buildMidiFile (k);
            }
            targetComp.repaint();
            return;
        }
    }
}

void MidiDragRelay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 4)
        return;

    if (cardIndex == 4 && ! pluckDragBounds.contains (e.getMouseDownPosition()) && ! pluckDragBounds.contains (e.getPosition()))
        return;

    dragStarted = true;
    hasGeneratedFromSession = true;
    const auto file = editor.buildMidiFile (cardIndex);
    if (file.existsAsFile())
    {
        juce::StringArray files;
        files.add (file.getFullPathName());
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, &targetComp);
    }
}

MagicalChordsAudioProcessorEditor::MagicalChordsAudioProcessorEditor (MagicalChordsAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (980, 720);

    titleLabel.setText ("MAGICAL CHORDS", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (25.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, colText);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("AI AUTO-PILOT HARMONY & MIDI  /  RYAN ZACK", juce::dontSendNotification);
    subtitleLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    subtitleLabel.setColour (juce::Label::textColourId, colAccent);
    addAndMakeVisible (subtitleLabel);

    listenBtn.setButtonText ("START LISTENING");
    listenBtn.setColour (juce::TextButton::buttonColourId, colBlue);
    listenBtn.setColour (juce::TextButton::textColourOffId, colText);
    listenBtn.setColour (juce::TextButton::textColourOnId, colText);
    listenBtn.onClick = [this]
    {
        if (audioProcessor.isAudioCapturing())
        {
            audioProcessor.stopAudioCapture();
            listenBtn.setButtonText ("START LISTENING");
            hasGeneratedFromSession = true;
            applyDetectedKeyAndScale();
        }
        else
        {
            resetRecordedWaveform();
            audioProcessor.resetAudioAnalysis();
            audioProcessor.startAudioCapture();
            listenBtn.setButtonText ("STOP LISTENING");
        }
        repaint();
    };
    addAndMakeVisible (listenBtn);

    importBtn.setButtonText ("IMPORT AUDIO");
    importBtn.setColour (juce::TextButton::buttonColourId, colElev);
    importBtn.setColour (juce::TextButton::textColourOffId, colText);
    importBtn.setColour (juce::TextButton::textColourOnId, colText);
    importBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Select Audio File",
                                                       juce::File{},
                                                       "*.wav;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                  {
                                      resetRecordedWaveform();
                                      audioProcessor.resetAudioAnalysis();
                                      // Decode and analyse the chosen file through the
                                      // same DSP path as live input.
                                      if (audioProcessor.loadAudioFile (fc.getResult()))
                                      {
                                          audioProcessor.startAudioCapture();
                                          listenBtn.setButtonText ("STOP LISTENING");
                                          applyDetectedKeyAndScale();
                                      }
                                      repaint();
                                  }
                              });
    };
    addAndMakeVisible (importBtn);

    resetBtn.setButtonText ("GENERATE CHORDS");
    resetBtn.setColour (juce::TextButton::buttonColourId, colAccent);
    resetBtn.setColour (juce::TextButton::textColourOffId, colBg);
    resetBtn.setColour (juce::TextButton::textColourOnId, colBg);
    resetBtn.onClick = [this]
    {
        hasGeneratedFromSession = true;
        applyDetectedKeyAndScale();
        repaint();
    };
    addAndMakeVisible (resetBtn);

    genreBox.addItemList ({ "Future Bass", "Pop", "EDM", "Lo-Fi", "Acoustic" }, 1);
    genreBox.onChange = [this]
    {
        syncToProcessor();
        if (hasGeneratedFromSession || aiAutoPilotEnabled)
        {
            hasGeneratedFromSession = true;
            for (int i = 0; i < 5; ++i)
                buildMidiFile (i);
        }
        repaint();
    };
    addAndMakeVisible (genreBox);

    keyBox.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    keyBox.onChange = [this]
    {
        syncToProcessor();
        if (hasGeneratedFromSession || aiAutoPilotEnabled)
        {
            hasGeneratedFromSession = true;
            for (int i = 0; i < 5; ++i)
                buildMidiFile (i);
        }
        repaint();
    };
    addAndMakeVisible (keyBox);

    scaleBox.addItemList ({ "Major", "Minor", "Dorian", "Mixolydian", "Phrygian", "Lydian", "Locrian" }, 1);
    scaleBox.onChange = [this]
    {
        syncToProcessor();
        if (hasGeneratedFromSession || aiAutoPilotEnabled)
        {
            hasGeneratedFromSession = true;
            for (int i = 0; i < 5; ++i)
                buildMidiFile (i);
        }
        repaint();
    };
    addAndMakeVisible (scaleBox);

    syncToggle.setColour (juce::ToggleButton::textColourId, colText);
    syncToggle.setColour (juce::ToggleButton::tickColourId, colAccent);
    syncToggle.onClick = [this] { syncToProcessor(); };
    addAndMakeVisible (syncToggle);

    bpmLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    bpmLabel.setColour (juce::Label::textColourId, colAccent);
    addAndMakeVisible (bpmLabel);

    const juce::StringArray names { "PIANO CHORDS", "BASSLINE", "LEAD MELODY", "DRUMS" };
    for (int i = 0; i < 4; ++i)
    {
        auto& c = cards[static_cast<size_t> (i)];
        c.name = names[i];

        c.prevButton.setColour (juce::TextButton::buttonColourId, colElev);
        c.prevButton.setColour (juce::TextButton::textColourOffId, colText);
        c.prevButton.setColour (juce::TextButton::textColourOnId, colText);
        c.prevButton.onClick = [this, i]
        {
            hasGeneratedFromSession = true;
            auto& card = cards[static_cast<size_t> (i)];
            card.variation = (card.variation + 14) % 15;
            if (i == 0)
                pluckVariation = (pluckVariation + 14) % 15;
            refreshCardLabels();
            syncToProcessor();
            buildMidiFile (i);
            if (i == 0)
                buildMidiFile (4);
        };
        addAndMakeVisible (c.prevButton);

        c.nextButton.setColour (juce::TextButton::buttonColourId, colElev);
        c.nextButton.setColour (juce::TextButton::textColourOffId, colText);
        c.nextButton.setColour (juce::TextButton::textColourOnId, colText);
        c.nextButton.onClick = [this, i]
        {
            hasGeneratedFromSession = true;
            auto& card = cards[static_cast<size_t> (i)];
            card.variation = (card.variation + 1) % 15;
            if (i == 0)
                pluckVariation = (pluckVariation + 1) % 15;
            refreshCardLabels();
            syncToProcessor();
            buildMidiFile (i);
            if (i == 0)
                buildMidiFile (4);
        };
        addAndMakeVisible (c.nextButton);

        c.dragButton.setColour (juce::TextButton::buttonColourId, colBlue);
        c.dragButton.setColour (juce::TextButton::textColourOffId, colText);
        c.dragButton.setColour (juce::TextButton::textColourOnId, colText);
        c.dragButton.onClick = [this, i]
        {
            hasGeneratedFromSession = true;
            buildMidiFile (i);
            repaint();
        };
        c.dragRelay = std::make_unique<MidiDragRelay> (*this, i, c.dragButton);
        c.dragButton.addMouseListener (c.dragRelay.get(), false);
        addAndMakeVisible (c.dragButton);
    }

    pluckDragRelay = std::make_unique<MidiDragRelay> (*this, 4, *this);
    addMouseListener (pluckDragRelay.get(), false);

    syncFromProcessor();
    refreshCardLabels();

    if (aiAutoPilotEnabled)
    {
        hasGeneratedFromSession = true;
        applyDetectedKeyAndScale();
    }

    startTimerHz (30);
}

MagicalChordsAudioProcessorEditor::~MagicalChordsAudioProcessorEditor()
{
    stopTimer();
    if (pluckDragRelay != nullptr)
    {
        removeMouseListener (pluckDragRelay.get());
        pluckDragRelay.reset();
    }
    for (auto& c : cards)
    {
        if (c.dragRelay != nullptr)
            c.dragButton.removeMouseListener (c.dragRelay.get());
    }
}

void MagicalChordsAudioProcessorEditor::applyDetectedKeyAndScale()
{
    const int windows = audioProcessor.getAnalyzedWindowCount();
    const double pitchHz = audioProcessor.getDetectedPitchHz();
    const float conf = audioProcessor.getPitchConfidence();

    int detectedRoot = keyBox.getSelectedItemIndex();
    bool detectedMinor = (scaleBox.getSelectedItemIndex() == 1);

    if (windows > 0 || conf > 0.01f || pitchHz > 25.0 || vocalTrailCount > 0)
    {
        detectedRoot = ((audioProcessor.getDetectedTonic() % 12) + 12) % 12;
        detectedMinor = audioProcessor.isDetectedMinor();
    }

    keyBox.setSelectedId (detectedRoot + 1, juce::dontSendNotification);

    int chosenScaleId = detectedMinor ? 2 : 1;
    const int vocalMidi = hzToMidiPitch (pitchHz);
    if (vocalMidi > 0)
    {
        const int pitchClass = ((vocalMidi - detectedRoot) % 12 + 12) % 12;
        if (! detectedMinor)
        {
            if (pitchClass == 6) chosenScaleId = 6;
            else if (pitchClass == 10) chosenScaleId = 4;
            else chosenScaleId = 1;
        }
        else
        {
            if (pitchClass == 1) chosenScaleId = 5;
            else if (pitchClass == 9) chosenScaleId = 3;
            else chosenScaleId = 2;
        }
    }

    scaleBox.setSelectedId (chosenScaleId, juce::dontSendNotification);

    const int vocalSig = (vocalTrailCount > 0)
                             ? (vocalTrailCount * 7 + (vocalMidi > 0 ? vocalMidi : 60) * 3 + windows)
                             : ((vocalMidi > 0 ? vocalMidi * 3 : detectedRoot * 5) + windows * 7 + 1);

    cards[0].variation = std::abs (vocalSig) % 15;
    cards[1].variation = std::abs (vocalSig * 2 + 3) % 15;
    cards[2].variation = std::abs (vocalSig * 3 + 7) % 15;
    cards[3].variation = std::abs (vocalSig * 5 + 11) % 15;
    pluckVariation     = std::abs (vocalSig * 7 + 13) % 15;

    refreshCardLabels();
    syncToProcessor();

    for (int i = 0; i < 5; ++i)
        buildMidiFile (i);
}

void MagicalChordsAudioProcessorEditor::syncFromProcessor()
{
    genreBox.setSelectedId (juce::jlimit (1, genreBox.getNumItems(), audioProcessor.savedGenreIndex + 1), juce::dontSendNotification);
    keyBox.setSelectedId (juce::jlimit (1, keyBox.getNumItems(), audioProcessor.savedKeyIndex + 1), juce::dontSendNotification);
    scaleBox.setSelectedId (juce::jlimit (1, scaleBox.getNumItems(), audioProcessor.savedScaleIndex + 1), juce::dontSendNotification);
    syncToggle.setToggleState (audioProcessor.savedDawSync, juce::dontSendNotification);

    cards[0].variation = juce::jlimit (0, 14, audioProcessor.savedPianoVar);
    cards[1].variation = juce::jlimit (0, 14, audioProcessor.savedBassVar);
    cards[2].variation = juce::jlimit (0, 14, audioProcessor.savedLeadVar);
    cards[3].variation = juce::jlimit (0, 14, audioProcessor.savedDrumsVar);
}

void MagicalChordsAudioProcessorEditor::syncToProcessor()
{
    audioProcessor.savedGenreIndex = juce::jmax (0, genreBox.getSelectedId() - 1);
    audioProcessor.savedKeyIndex   = juce::jmax (0, keyBox.getSelectedId() - 1);
    audioProcessor.savedScaleIndex = juce::jmax (0, scaleBox.getSelectedId() - 1);
    audioProcessor.savedDawSync    = syncToggle.getToggleState();

    audioProcessor.savedPianoVar = cards[0].variation;
    audioProcessor.savedBassVar  = cards[1].variation;
    audioProcessor.savedLeadVar  = cards[2].variation;
    audioProcessor.savedDrumsVar = cards[3].variation;
}

void MagicalChordsAudioProcessorEditor::refreshCardLabels()
{
    for (auto& c : cards)
        c.dragButton.setButtonText ("DRAG MIDI (" + juce::String (c.variation + 1) + "/15) >>");

    repaint();
}

juce::File MagicalChordsAudioProcessorEditor::buildMidiFile (int cardIndex)
{
    const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto outFile = tempDir.getChildFile ("MagicalChords_" + juce::String (cardIndex + 1) + ".mid");

    juce::MidiMessageSequence seq;
    const double ticksPerQuarter = 960.0;
    const double barTicks = 4.0 * ticksPerQuarter;

    const int tonic = keyBox.getSelectedItemIndex();
    const int scaleIdx = scaleBox.getSelectedItemIndex();
    const int genreIdx = genreBox.getSelectedItemIndex();
    const int barCount = getDynamicBarCount();
    const int varIdx = (cardIndex < 4) ? cards[static_cast<size_t> (cardIndex)].variation : pluckVariation;
    const int semiShift = (cardIndex >= 0 && cardIndex < 5) ? stemSemitoneShift[static_cast<size_t> (cardIndex)] : 0;

    const auto scale = getScaleIntervals (scaleIdx);
    const int sz = static_cast<int> (scale.size());

    std::vector<int> barDegrees;
    std::vector<int> stepMelodyDegs;
    float vocalDensity = 0.25f;
    computeVocalPhrasePlan (tonic,
                           scaleIdx,
                           barCount,
                           varIdx,
                           audioProcessor.getDetectedPitchHz(),
                           barDegrees,
                           stepMelodyDegs,
                           vocalDensity);

    if (cardIndex == 0)
    {
        const int baseMidi = juce::jlimit (36, 96, 60 + tonic + semiShift);
        const bool brokenChord = (varIdx % 2 == 1);

        for (int bar = 0; bar < barCount; ++bar)
        {
            const int deg = barDegrees[static_cast<size_t> (bar)] % sz;
            const int root = juce::jlimit (24, 108, baseMidi + scale[static_cast<size_t> (deg)] + ((bar % 4 == 2 && varIdx % 3 == 0) ? 12 : 0));
            const int thirdOffset = scale[static_cast<size_t> ((deg + 2) % sz)] - scale[static_cast<size_t> (deg)];
            const int fifthOffset = scale[static_cast<size_t> ((deg + 4) % sz)] - scale[static_cast<size_t> (deg)];
            const int third = juce::jlimit (24, 108, root + thirdOffset);
            const int fifth = juce::jlimit (24, 108, root + fifthOffset);
            const int ext = juce::jlimit (24, 108, root + ((chordComplexityMode == 0) ? 12 : ((bar % 2 == 0) ? 14 : 11)));

            const double barStart = bar * barTicks;
            if (! brokenChord)
            {
                const int hits = (genreIdx == 2 || vocalDensity > 0.65f) ? 2 : 1;
                const double hitLen = (barTicks / hits) * 0.9;
                for (int h = 0; h < hits; ++h)
                {
                    const double t0 = barStart + h * (barTicks / hits);
                    const juce::uint8 vel = static_cast<juce::uint8> (juce::jlimit (65, 118, 92 + static_cast<int> (vocalDensity * 18.0f)));
                    seq.addEvent (juce::MidiMessage::noteOn (1, root, vel), t0);
                    seq.addEvent (juce::MidiMessage::noteOn (1, third, vel), t0);
                    seq.addEvent (juce::MidiMessage::noteOn (1, fifth, vel), t0);
                    if (chordComplexityMode == 1)
                        seq.addEvent (juce::MidiMessage::noteOn (1, ext, static_cast<juce::uint8> (vel - 10)), t0);

                    seq.addEvent (juce::MidiMessage::noteOff (1, root), t0 + hitLen);
                    seq.addEvent (juce::MidiMessage::noteOff (1, third), t0 + hitLen);
                    seq.addEvent (juce::MidiMessage::noteOff (1, fifth), t0 + hitLen);
                    if (chordComplexityMode == 1)
                        seq.addEvent (juce::MidiMessage::noteOff (1, ext), t0 + hitLen);
                }
            }
            else
            {
                const int chordNotes[4] { root, third, fifth, ext };
                const int count = (chordComplexityMode == 0) ? 3 : 4;
                const double stepLen = ticksPerQuarter * 0.9;
                for (int s = 0; s < count; ++s)
                {
                    const double t0 = barStart + s * ticksPerQuarter;
                    seq.addEvent (juce::MidiMessage::noteOn (1, chordNotes[s], (juce::uint8) 96), t0);
                    seq.addEvent (juce::MidiMessage::noteOff (1, chordNotes[s]), t0 + stepLen);
                }
            }
        }
    }
    else if (cardIndex == 1)
    {
        const int baseBass = juce::jlimit (24, 72, 36 + tonic + semiShift);
        const int subSteps = (genreIdx == 2 || vocalDensity > 0.6f) ? 8 : 4;
        const double stepTicks = barTicks / subSteps;

        for (int bar = 0; bar < barCount; ++bar)
        {
            const int deg = barDegrees[static_cast<size_t> (bar)] % sz;
            const int root = juce::jlimit (24, 84, baseBass + scale[static_cast<size_t> (deg)]);
            const int fifth = juce::jlimit (24, 84, root + scale[static_cast<size_t> ((deg + 4) % sz)] - scale[static_cast<size_t> (deg)]);
            const int octave = juce::jlimit (24, 96, root + 12);

            for (int s = 0; s < subSteps; ++s)
            {
                int n = root;
                if (s == subSteps - 1)
                    n = fifth;
                else if (subSteps == 8 && (s == 3 || s == 6))
                    n = octave;
                else if (subSteps == 4 && s == 2)
                    n = (bar % 2 == 0) ? octave : fifth;

                const double t0 = bar * barTicks + s * stepTicks;
                seq.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 108), t0);
                seq.addEvent (juce::MidiMessage::noteOff (1, n), t0 + stepTicks * 0.85);
            }
        }
    }
    else if (cardIndex == 2)
    {
        const int vocalMidi = hzToMidiPitch (audioProcessor.getDetectedPitchHz());
        const int baseLead = (vocalMidi > 48) ? juce::jlimit (60, 84, vocalMidi) : (72 + tonic);
        const int octaveBase = ((baseLead - (60 + tonic)) / 12) * 12 + semiShift;
        const int notesPerBar = (genreIdx == 0 || genreIdx == 2) ? 8 : 4;
        const double stepTicks = barTicks / notesPerBar;

        for (int bar = 0; bar < barCount; ++bar)
        {
            for (int nIdx = 0; nIdx < notesPerBar; ++nIdx)
            {
                const int mStep = juce::jlimit (0, static_cast<int> (stepMelodyDegs.size()) - 1,
                                               bar * 4 + (nIdx * 4) / std::max (1, notesPerBar));
                const int scalePos = stepMelodyDegs[static_cast<size_t> (mStep)] % sz;
                const int octaveShift = ((nIdx + bar) % 6 == 0 && vocalDensity > 0.35f) ? 12 : 0;
                const int note = juce::jlimit (36, 108, 60 + tonic + scale[static_cast<size_t> (scalePos)] + octaveShift + octaveBase);

                const double t0 = bar * barTicks + nIdx * stepTicks;
                seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 104), t0);
                seq.addEvent (juce::MidiMessage::noteOff (1, note), t0 + stepTicks * 0.78);
            }
        }
    }
    else if (cardIndex == 3)
    {
        const int steps = 16;
        const double stepTicks = barTicks / steps;
        const int drumShift = juce::jlimit (-12, 12, semiShift);
        const int kickNote = juce::jlimit (24, 96, 36 + drumShift);
        const int snareNote = juce::jlimit (24, 96, 38 + drumShift);
        const int hatNote = juce::jlimit (24, 96, 42 + drumShift);
        const int openHatNote = juce::jlimit (24, 96, 46 + drumShift);

        for (int bar = 0; bar < barCount; ++bar)
        {
            const bool isTurnaround = ((bar + 1) % 4 == 0);
            for (int s = 0; s < steps; ++s)
            {
                const double t0 = bar * barTicks + s * stepTicks;
                const bool kick = (s == 0 || s == 8 || (vocalDensity > 0.45f && s == 6) || (genreIdx == 2 && s % 4 == 0) || (isTurnaround && s == 14));
                const bool snare = (s == 4 || s == 12 || (isTurnaround && s == 15));
                const bool hat = (s % 2 == 0) || (vocalDensity > 0.4f && s % 2 == 1);
                const bool openHat = (s == 7 || (isTurnaround && s == 11));

                if (kick)
                {
                    seq.addEvent (juce::MidiMessage::noteOn (10, kickNote, (juce::uint8) 116), t0);
                    seq.addEvent (juce::MidiMessage::noteOff (10, kickNote), t0 + stepTicks * 0.7);
                }
                if (snare)
                {
                    seq.addEvent (juce::MidiMessage::noteOn (10, snareNote, (juce::uint8) 108), t0);
                    seq.addEvent (juce::MidiMessage::noteOff (10, snareNote), t0 + stepTicks * 0.7);
                }
                if (hat)
                {
                    seq.addEvent (juce::MidiMessage::noteOn (10, hatNote, (juce::uint8) 78), t0);
                    seq.addEvent (juce::MidiMessage::noteOff (10, hatNote), t0 + stepTicks * 0.45);
                }
                if (openHat)
                {
                    seq.addEvent (juce::MidiMessage::noteOn (10, openHatNote, (juce::uint8) 88), t0);
                    seq.addEvent (juce::MidiMessage::noteOff (10, openHatNote), t0 + stepTicks * 0.8);
                }
            }
        }
    }
    else
    {
        const double bpm = juce::jlimit (60.0, 200.0, audioProcessor.savedManualBpm);
        const int pluckSteps = (bpm > 140.0) ? 4 : 8;
        const double stepTicks = barTicks / pluckSteps;
        const int basePluck = juce::jlimit (36, 96, 60 + tonic + semiShift);
        const int arpPattern[4] { 0, 2, 4, 2 };

        for (int bar = 0; bar < barCount; ++bar)
        {
            const int deg = barDegrees[static_cast<size_t> (bar)] % sz;
            for (int s = 0; s < pluckSteps; ++s)
            {
                const int mStep = juce::jlimit (0, static_cast<int> (stepMelodyDegs.size()) - 1, bar * 4 + (s / 2));
                const int melodyBias = (s % 2 == 0) ? (stepMelodyDegs[static_cast<size_t> (mStep)] % sz) : ((deg + arpPattern[(s + varIdx) % 4]) % sz);
                const int octUp = ((s + bar + varIdx) % 8 == 6 && genreIdx == 0 && chordComplexityMode == 1) ? 12 : 0;
                const int note = juce::jlimit (36, 108, basePluck + scale[static_cast<size_t> (melodyBias)] + octUp);

                const double t0 = bar * barTicks + s * stepTicks;
                const juce::uint8 vel = static_cast<juce::uint8> ((s % 2 == 0) ? 104 : 84);
                seq.addEvent (juce::MidiMessage::noteOn (1, note, vel), t0);
                seq.addEvent (juce::MidiMessage::noteOff (1, note), t0 + stepTicks * 0.55);
            }
        }
    }

    seq.sort();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote ((short) ticksPerQuarter);
    midiFile.addTrack (seq);

    juce::FileOutputStream stream (outFile);
    if (stream.openedOk())
    {
        stream.setPosition (0);
        stream.truncate();
        midiFile.writeTo (stream);
    }

    return outFile;
}

void MagicalChordsAudioProcessorEditor::timerCallback()
{
    if (audioProcessor.isAudioCapturing())
    {
        std::array<float, 256> liveChunk {};
        audioProcessor.copyLiveWaveform (liveChunk);
        appendToRecordedWaveform (liveChunk,
                                  audioProcessor.getDetectedPitchHz(),
                                  audioProcessor.getPitchConfidence());
    }

    if (syncToggle.getToggleState() && audioProcessor.hasValidHostBpm())
        audioProcessor.savedManualBpm = audioProcessor.getHostBpm();

    if (aiAutoPilotEnabled)
    {
        hasGeneratedFromSession = true;
        const int curFrames = std::max (vocalTrailCount, voicedTimerTicks);
        const double curPitch = audioProcessor.getDetectedPitchHz();
        const bool pitchShifted = (curPitch > 25.0)
                                    && (lastAutoTriggeredPitch < 25.0 || std::abs (curPitch - lastAutoTriggeredPitch) > 12.0);
        const bool enoughNewFrames = (curFrames > 0) && (std::abs (curFrames - lastAutoTriggeredFrames) >= 24);

        if (pitchShifted || enoughNewFrames)
        {
            lastAutoTriggeredFrames = curFrames;
            lastAutoTriggeredPitch = curPitch;
            applyDetectedKeyAndScale();
        }
    }

    const int activeBars = hasGeneratedFromSession ? getDynamicBarCount() : 4;
    bpmLabel.setText ("BPM: " + juce::String (audioProcessor.savedManualBpm, 1)
                      + (hasGeneratedFromSession ? ("  (" + juce::String (activeBars) + " BARS)") : "  (INIT)")
                      + (syncToggle.getToggleState() ? " [SYNC]" : " [MAN]")
                      + (aiAutoPilotEnabled ? " [AI]" : ""),
                      juce::dontSendNotification);

    repaint();
}

void MagicalChordsAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colBg);

    const int w = getWidth();

    const auto banner = juce::Rectangle<int> (14, 10, w - 28, 56);
    g.setColour (colPanel);
    g.fillRoundedRectangle (banner.toFloat(), 8.0f);
    g.setColour (colBorder);
    g.drawRoundedRectangle (banner.toFloat(), 8.0f, 1.0f);

    const auto sec1 = juce::Rectangle<int> (14, 74, w - 28, 62);
    g.setColour (colPanel);
    g.fillRoundedRectangle (sec1.toFloat(), 8.0f);
    g.setColour (colBorder);
    g.drawRoundedRectangle (sec1.toFloat(), 8.0f, 1.0f);

    g.setColour (colAccent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("01 / AUDIO INPUT", sec1.getX() + 12, sec1.getY() + 4, 160, 14, juce::Justification::left);

    const double pitchHz = audioProcessor.getDetectedPitchHz();
    const float conf = audioProcessor.getPitchConfidence();
    const int confPct = juce::jlimit (0, 100, static_cast<int> (std::llround (conf * 100.0f)));
    const bool capturing = audioProcessor.isAudioCapturing();

    const juce::String pitchInfo = (capturing ? "LIVE INPUT" : (hasGeneratedFromSession ? "VOCAL ANALYZED" : "INITIAL READY"))
                                   + juce::String ("  •  ") + freqToNoteLabel (pitchHz)
                                   + juce::String ("  •  ") + juce::String (pitchHz > 25.0 ? pitchHz : 0.0, 1) + " Hz"
                                   + juce::String ("  •  conf ") + juce::String (confPct) + "%";

    g.setColour (capturing ? colAccent : colText);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (pitchInfo, 445, sec1.getY() + 26, 300, 24, juce::Justification::left);

    const auto sec2 = juce::Rectangle<int> (14, 144, w - 28, 104);
    g.setColour (colPanel);
    g.fillRoundedRectangle (sec2.toFloat(), 8.0f);
    g.setColour (colBorder);
    g.drawRoundedRectangle (sec2.toFloat(), 8.0f, 1.0f);

    g.setColour (colAccent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("02 / WAVEFORM PREVIEW  (" + juce::String (hasGeneratedFromSession ? getDynamicBarCount() : 0) + " BARS)",
                sec2.getX() + 12, sec2.getY() + 5, 260, 14, juce::Justification::left);

    const auto waveInner = juce::Rectangle<int> (sec2.getX() + 12, sec2.getY() + 22, sec2.getWidth() - 24, sec2.getHeight() - 30);
    g.setColour (colBg);
    g.fillRoundedRectangle (waveInner.toFloat(), 6.0f);
    g.setColour (colBorder);
    g.drawRoundedRectangle (waveInner.toFloat(), 6.0f, 1.0f);

    std::array<float, 256> liveWave {};
    audioProcessor.copyLiveWaveform (liveWave);

    float livePeak = 0.0f;
    for (float s : liveWave)
        livePeak = std::max (livePeak, std::abs (s));

    float recPeak = 0.0f;
    for (int i = 0; i < recordedWaveCount; ++i)
        recPeak = std::max (recPeak, std::abs (getOrderedRecordedSample (i)));

    const float meterLevel = capturing ? std::max (livePeak, recPeak * 0.75f) : (recPeak * 0.65f);
    const int meterBars = 22;
    const int activeBars = juce::jlimit (0, meterBars, static_cast<int> (std::ceil (std::min (1.0f, meterLevel * 6.0f) * meterBars)));
    for (int b = 0; b < meterBars; ++b)
    {
        const int bx = w - 215 + b * 8;
        const int by = sec1.getY() + 28;
        const juce::Colour barCol = (b < activeBars)
                                        ? (b > 17 ? colAmber : (b > 12 ? juce::Colour { 0xffa3e635 } : colAccent))
                                        : colElev;
        g.setColour (barCol);
        g.fillRoundedRectangle (static_cast<float> (bx), static_cast<float> (by), 5.0f, 20.0f, 1.5f);
    }

    const float midY = static_cast<float> (waveInner.getCentreY());
    const float halfH = static_cast<float> (waveInner.getHeight()) * 0.44f;
    const float xStart = static_cast<float> (waveInner.getX() + 8);
    const float widthF = static_cast<float> (waveInner.getWidth() - 16);

    const bool showRecorded = hasRecordedWave && (recordedWaveCount > 0) && (recPeak > 0.00015f);
    const bool showLive = capturing && ! showRecorded && (livePeak > 0.00015f);

    if (showRecorded)
    {
        const float gain = std::min (18.0f, 0.95f / recPeak);
        const int cols = std::min (220, std::max (24, recordedWaveCount));
        const float colStep = widthF / static_cast<float> (cols);

        for (int cIdx = 0; cIdx < cols; ++cIdx)
        {
            const int sIdx = (cIdx * recordedWaveCount) / cols;
            const float amp = juce::jlimit (0.06f, 1.0f, std::abs (getOrderedRecordedSample (sIdx)) * gain);
            const float barH = amp * halfH * 2.0f;
            const float bx = xStart + static_cast<float> (cIdx) * colStep;
            const float by = midY - barH * 0.5f;

            g.setColour (colAccent.withAlpha (0.9f));
            g.fillRoundedRectangle (bx, by, std::max (1.5f, colStep * 0.7f), barH, 1.0f);
        }
    }
    else if (showLive)
    {
        const float gain = std::min (18.0f, 0.95f / livePeak);
        const int cols = 128;
        const float colStep = widthF / static_cast<float> (cols);

        for (int cIdx = 0; cIdx < cols; ++cIdx)
        {
            const size_t sIdx = static_cast<size_t> ((cIdx * static_cast<int> (liveWave.size())) / cols);
            const float amp = juce::jlimit (0.06f, 1.0f, std::abs (liveWave[sIdx]) * gain);
            const float barH = amp * halfH * 2.0f;
            const float bx = xStart + static_cast<float> (cIdx) * colStep;
            const float by = midY - barH * 0.5f;

            g.setColour (colAccent.withAlpha (0.9f));
            g.fillRoundedRectangle (bx, by, std::max (1.5f, colStep * 0.7f), barH, 1.0f);
        }
    }
    else
    {
        g.setColour (colMuted.withAlpha (0.55f));
        g.drawHorizontalLine (static_cast<int> (midY), xStart, xStart + widthF);
        g.setColour (colMuted);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("INITIAL — Click START LISTENING or IMPORT AUDIO to record & preview vocal waveform",
                    waveInner, juce::Justification::centred);
    }

    const auto sec3 = juce::Rectangle<int> (14, 256, w - 28, 134);
    g.setColour (colPanel);
    g.fillRoundedRectangle (sec3.toFloat(), 8.0f);
    g.setColour (colBorder);
    g.drawRoundedRectangle (sec3.toFloat(), 8.0f, 1.0f);

    g.setColour (colAccent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("03 / CHORD PROGRESSION", sec3.getX() + 12, sec3.getY() + 5, 210, 14, juce::Justification::left);

    aiAutoBadgeBounds = juce::Rectangle<int> (sec3.getRight() - 256, sec3.getY() + 4, 118, 16);
    g.setColour (aiAutoPilotEnabled ? colAccent.withAlpha (0.25f) : colElev);
    g.fillRoundedRectangle (aiAutoBadgeBounds.toFloat(), 4.0f);
    g.setColour (aiAutoPilotEnabled ? colAccent : colMuted);
    g.drawRoundedRectangle (aiAutoBadgeBounds.toFloat(), 4.0f, 1.0f);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (aiAutoPilotEnabled ? "AI AUTO: ON" : "AI AUTO: OFF", aiAutoBadgeBounds, juce::Justification::centred);

    chordModeBadgeBounds = juce::Rectangle<int> (sec3.getRight() - 130, sec3.getY() + 4, 118, 16);
    g.setColour (chordComplexityMode == 1 ? colPurple.withAlpha (0.28f) : colElev);
    g.fillRoundedRectangle (chordModeBadgeBounds.toFloat(), 4.0f);
    g.setColour (chordComplexityMode == 1 ? colText : colMuted);
    g.drawRoundedRectangle (chordModeBadgeBounds.toFloat(), 4.0f, 1.0f);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (chordComplexityMode == 1 ? "MODE: ADV PRO" : "MODE: BASIC", chordModeBadgeBounds, juce::Justification::centred);

    const int tonic = keyBox.getSelectedItemIndex();
    const int scaleIdx = scaleBox.getSelectedItemIndex();
    const int barCount = getDynamicBarCount();
    const auto scale = getScaleIntervals (scaleIdx);
    const int sz = static_cast<int> (scale.size());
    const int varIdx = cards[0].variation;

    const juce::Colour bigChordCols[4] { colBlue, colPurple, colAccent, colAmber };
    const int chordRowTop = sec3.getY() + 62;
    const int chordRowH = 60;
    const int chordGap = 12;
    const int chordAreaW = sec3.getWidth() - 24;
    const int chordCardW = (chordAreaW - chordGap * 3) / 4;

    if (! hasGeneratedFromSession)
    {
        for (int i = 0; i < 4; ++i)
        {
            const auto box = juce::Rectangle<int> (sec3.getX() + 12 + i * (chordCardW + chordGap),
                                                 chordRowTop,
                                                 chordCardW,
                                                 chordRowH);
            g.setColour (colBg);
            g.fillRoundedRectangle (box.toFloat(), 8.0f);
            g.setColour (colBorder);
            g.drawRoundedRectangle (box.toFloat(), 8.0f, 1.0f);
            g.setColour (colMuted);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("--", box, juce::Justification::centred);
        }
    }
    else
    {
        std::vector<int> barDegrees;
        std::vector<int> stepMelodyDegs;
        float vocalDensity = 0.25f;
        computeVocalPhrasePlan (tonic,
                               scaleIdx,
                               barCount,
                               varIdx,
                               pitchHz,
                               barDegrees,
                               stepMelodyDegs,
                               vocalDensity);

        const int displayBars = std::min (4, barCount);
        for (int i = 0; i < displayBars; ++i)
        {
            const int deg = barDegrees[static_cast<size_t> (i)] % sz;
            const juce::String symbol = getChordSymbol (tonic, scaleIdx, deg, i + varIdx);
            const auto box = juce::Rectangle<int> (sec3.getX() + 12 + i * (chordCardW + chordGap),
                                                 chordRowTop,
                                                 chordCardW,
                                                 chordRowH);

            g.setColour (bigChordCols[i].withAlpha (0.22f));
            g.fillRoundedRectangle (box.toFloat(), 8.0f);
            g.setColour (bigChordCols[i]);
            g.drawRoundedRectangle (box.toFloat(), 8.0f, 1.5f);

            g.setColour (colText);
            g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
            g.drawText (symbol, box, juce::Justification::centred);
        }
    }

    const auto sec4 = juce::Rectangle<int> (14, 398, w - 28, getHeight() - 410);
    g.setColour (colPanel);
    g.fillRoundedRectangle (sec4.toFloat(), 8.0f);
    g.setColour (colBorder);
    g.drawRoundedRectangle (sec4.toFloat(), 8.0f, 1.0f);

    g.setColour (colAccent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("04 / GENERATED MIDI STEMS (" + juce::String (hasGeneratedFromSession ? barCount : 0) + " BARS)  •  SEMI (-12..+12)",
                sec4.getX() + 12, sec4.getY() + 5, 560, 14, juce::Justification::left);

    const double totalBeats = static_cast<double> (barCount) * 4.0;

    for (size_t idx = 0; idx < cards.size(); ++idx)
    {
        const auto& c = cards[idx];
        g.setColour (colCard);
        g.fillRoundedRectangle (c.area.toFloat(), 8.0f);
        g.setColour (colBorder);
        g.drawRoundedRectangle (c.area.toFloat(), 8.0f, 1.0f);

        auto textBox = c.area.reduced (8, 6);
        g.setColour (colAccent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (c.name, textBox.removeFromTop (16), juce::Justification::left);

        const int shiftVal = stemSemitoneShift[idx];
        const juce::String shiftText = (shiftVal > 0 ? "+" : "") + juce::String (shiftVal) + " st";
        semiBadgeBounds[idx] = juce::Rectangle<int> (c.area.getRight() - 62, c.area.getY() + 6, 54, 16);
        g.setColour (shiftVal != 0 ? colPurple.withAlpha (0.35f) : colElev);
        g.fillRoundedRectangle (semiBadgeBounds[idx].toFloat(), 4.0f);
        g.setColour (shiftVal != 0 ? colText : colMuted);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (shiftText, semiBadgeBounds[idx], juce::Justification::centred);

        g.setColour (colMuted);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (hasGeneratedFromSession ? ("Var " + juce::String (c.variation + 1) + "/15") : "INITIAL",
                    textBox.removeFromTop (14), juce::Justification::left);

        const auto rollArea = juce::Rectangle<int> (c.area.getX() + 8,
                                                  c.area.getY() + 38,
                                                  c.area.getWidth() - 16,
                                                  c.area.getHeight() - 74);
        if (rollArea.getHeight() > 14)
        {
            g.setColour (colBg);
            g.fillRoundedRectangle (rollArea.toFloat(), 5.0f);
            g.setColour (colBorder);
            g.drawRoundedRectangle (rollArea.toFloat(), 5.0f, 1.0f);

            const auto innerRoll = rollArea.reduced (4, 4);
            for (int b = 0; b <= 4; ++b)
            {
                const float gx = static_cast<float> (innerRoll.getX())
                                 + (static_cast<float> (b) / 4.0f) * static_cast<float> (innerRoll.getWidth());
                g.setColour (colElev);
                g.drawVerticalLine (static_cast<int> (gx),
                                    static_cast<float> (innerRoll.getY()),
                                    static_cast<float> (innerRoll.getBottom()));
            }

            if (hasGeneratedFromSession)
            {
                const auto midiFile = buildMidiFile (static_cast<int> (idx));
                juce::MidiFile parsedMidi;
                std::vector<VisualNote> notes;
                if (midiFile.existsAsFile())
                {
                    juce::FileInputStream in (midiFile);
                    if (in.openedOk() && parsedMidi.readFrom (in))
                    {
                        if (const auto* track = parsedMidi.getTrack (0))
                            notes = extractNotesFromSequence (*track, parsedMidi.getTimeFormat() > 0 ? parsedMidi.getTimeFormat() : 960.0);
                    }
                }

                if (! notes.empty())
                {
                    int minNote = notes.front().midiNote;
                    int maxNote = notes.front().midiNote;
                    for (const auto& n : notes)
                    {
                        minNote = std::min (minNote, n.midiNote);
                        maxNote = std::max (maxNote, n.midiNote);
                    }
                    if (maxNote == minNote)
                    {
                        minNote -= 2;
                        maxNote += 2;
                    }
                    const double span = static_cast<double> (maxNote - minNote + 1);
                    const float noteH = std::max (2.5f, std::min (7.0f, (static_cast<float> (innerRoll.getHeight()) / static_cast<float> (span)) * 0.85f));
                    const juce::Colour noteCol = (idx == 0) ? colBlue
                                               : (idx == 1) ? colAccent
                                               : (idx == 2) ? colPurple
                                                            : colAmber;

                    for (const auto& n : notes)
                    {
                        const float nx = static_cast<float> (innerRoll.getX())
                                         + static_cast<float> (std::max (0.0, n.startBeat) / totalBeats) * static_cast<float> (innerRoll.getWidth());
                        const float nw = std::max (2.0f, static_cast<float> (n.lengthBeats / totalBeats) * static_cast<float> (innerRoll.getWidth()) - 1.0f);
                        const double normPitch = static_cast<double> (n.midiNote - minNote) / span;
                        const float ny = static_cast<float> (innerRoll.getBottom())
                                         - static_cast<float> (normPitch) * static_cast<float> (innerRoll.getHeight() - noteH)
                                         - noteH;

                        g.setColour (noteCol.withAlpha (0.9f));
                        g.fillRoundedRectangle (nx, ny, nw, noteH, 1.5f);
                    }
                }
            }
            else
            {
                g.setColour (colMuted);
                g.setFont (juce::FontOptions (10.5f));
                g.drawText ("EMPTY", innerRoll, juce::Justification::centred);
            }
        }
    }

    const auto& pArea = pluckCardBounds[4];
    g.setColour (colCard);
    g.fillRoundedRectangle (pArea.toFloat(), 8.0f);
    g.setColour (colPink.withAlpha (0.75f));
    g.drawRoundedRectangle (pArea.toFloat(), 8.0f, 1.2f);

    g.setColour (colPink);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("PLUCK", pArea.getX() + 8, pArea.getY() + 6, 90, 16, juce::Justification::left);

    const int pShift = stemSemitoneShift[4];
    const juce::String pShiftText = (pShift > 0 ? "+" : "") + juce::String (pShift) + " st";
    semiBadgeBounds[4] = juce::Rectangle<int> (pArea.getRight() - 62, pArea.getY() + 6, 54, 16);
    g.setColour (pShift != 0 ? colPink.withAlpha (0.35f) : colElev);
    g.fillRoundedRectangle (semiBadgeBounds[4].toFloat(), 4.0f);
    g.setColour (pShift != 0 ? colText : colMuted);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (pShiftText, semiBadgeBounds[4], juce::Justification::centred);

    g.setColour (colMuted);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (hasGeneratedFromSession ? ("Var " + juce::String (pluckVariation + 1) + "/15") : "INITIAL",
                pArea.getX() + 8, pArea.getY() + 22, 90, 14, juce::Justification::left);

    const auto pRoll = juce::Rectangle<int> (pArea.getX() + 8, pArea.getY() + 38, pArea.getWidth() - 16, pArea.getHeight() - 74);
    if (pRoll.getHeight() > 14)
    {
        g.setColour (colBg);
        g.fillRoundedRectangle (pRoll.toFloat(), 5.0f);
        g.setColour (colBorder);
        g.drawRoundedRectangle (pRoll.toFloat(), 5.0f, 1.0f);

        if (hasGeneratedFromSession)
        {
            const auto pMidi = buildMidiFile (4);
            juce::MidiFile parsedPluck;
            std::vector<VisualNote> pNotes;
            if (pMidi.existsAsFile())
            {
                juce::FileInputStream in (pMidi);
                if (in.openedOk() && parsedPluck.readFrom (in))
                {
                    if (const auto* track = parsedPluck.getTrack (0))
                        pNotes = extractNotesFromSequence (*track, parsedPluck.getTimeFormat() > 0 ? parsedPluck.getTimeFormat() : 960.0);
                }
            }

            if (! pNotes.empty())
            {
                int minN = pNotes.front().midiNote;
                int maxN = pNotes.front().midiNote;
                for (const auto& n : pNotes)
                {
                    minN = std::min (minN, n.midiNote);
                    maxN = std::max (maxN, n.midiNote);
                }
                if (maxN == minN) { minN -= 2; maxN += 2; }
                const double span = static_cast<double> (maxN - minN + 1);
                const float nh = std::max (2.5f, std::min (6.5f, (static_cast<float> (pRoll.getHeight()) / static_cast<float> (span)) * 0.85f));
                for (const auto& n : pNotes)
                {
                    const float nx = static_cast<float> (pRoll.getX()) + static_cast<float> (std::max (0.0, n.startBeat) / totalBeats) * static_cast<float> (pRoll.getWidth());
                    const float nw = std::max (2.0f, static_cast<float> (n.lengthBeats / totalBeats) * static_cast<float> (pRoll.getWidth()) - 1.0f);
                    const double normP = static_cast<double> (n.midiNote - minN) / span;
                    const float ny = static_cast<float> (pRoll.getBottom()) - static_cast<float> (normP) * static_cast<float> (pRoll.getHeight() - nh) - nh;
                    g.setColour (colPink.withAlpha (0.9f));
                    g.fillRoundedRectangle (nx, ny, nw, nh, 1.5f);
                }
            }
        }
        else
        {
            g.setColour (colMuted);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText ("EMPTY", pRoll, juce::Justification::centred);
        }
    }

    pluckDragBounds = juce::Rectangle<int> (pArea.getX() + 8, pArea.getBottom() - 32, pArea.getWidth() - 16, 24);
    g.setColour (colPink.withAlpha (0.28f));
    g.fillRoundedRectangle (pluckDragBounds.toFloat(), 5.0f);
    g.setColour (colPink);
    g.drawRoundedRectangle (pluckDragBounds.toFloat(), 5.0f, 1.0f);
    g.setColour (colText);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("DRAG PLUCK MIDI >>", pluckDragBounds, juce::Justification::centred);
}

void MagicalChordsAudioProcessorEditor::resized()
{
    const int w = getWidth();

    titleLabel.setBounds (28, 14, 260, 26);
    subtitleLabel.setBounds (28, 40, 380, 18);

    listenBtn.setBounds (26, 98, 165, 28);
    importBtn.setBounds (199, 98, 140, 28);
    resetBtn.setBounds (w - 186, 98, 160, 28);

    const int ctrlY = 278;
    keyBox.setBounds (26, ctrlY, 76, 26);
    scaleBox.setBounds (110, ctrlY, 128, 26);
    genreBox.setBounds (246, ctrlY, 140, 26);
    syncToggle.setBounds (394, ctrlY, 108, 26);
    bpmLabel.setBounds (508, ctrlY, 240, 26);

    const auto sec4 = juce::Rectangle<int> (26, 420, w - 52, getHeight() - 434);
    const int gap = 8;
    const int cols = 5;
    const int cardW = (sec4.getWidth() - gap * (cols - 1)) / cols;
    const int cardH = sec4.getHeight();

    for (int i = 0; i < 4; ++i)
    {
        auto& c = cards[static_cast<size_t> (i)];
        c.area = juce::Rectangle<int> (sec4.getX() + i * (cardW + gap), sec4.getY(), cardW, cardH);

        auto bottom = c.area.reduced (8, 6).removeFromBottom (24);
        c.prevButton.setBounds (bottom.removeFromLeft (24));
        bottom.removeFromLeft (3);
        c.nextButton.setBounds (bottom.removeFromLeft (24));
        bottom.removeFromLeft (4);
        c.dragButton.setBounds (bottom);
    }

    pluckCardBounds[4] = juce::Rectangle<int> (sec4.getX() + 4 * (cardW + gap), sec4.getY(), cardW, cardH);
}
