# Magical Chords — by Ryan Zack
**64-Bit VST3 AI Audio-to-MIDI & Harmonic Arrangement Suite**

Magical Chords listens to a live vocal (or any monophonic audio) and, in real
time, generates a full chord progression plus five premium MIDI stems — Piano
Chords, Bassline, Lead Melody, Drums and a Pluck arp — that you drag straight
into any DAW (Ableton Live, FL Studio, Logic Pro, Cubase, Studio One, Pro Tools,
Reaper, Bitwig). An AI Auto-Pilot mode re-generates the whole arrangement every
time the vocal moves to a new phrase.

---

## Key features

- **Real-time vocal pitch & key detection.** A worker thread runs a YIN pitch
  estimator off the audio callback, accumulates a weighted pitch-class
  histogram and estimates the key (tonic + major/minor) with the
  Krumhansl–Schmuckler algorithm — never blocking the audio thread.
- **Dual-mode DAW sync.** `DAW SYNC` reads host BPM; one-click manual override
  for Key, Scale and Tempo.
- **Genre selector.** Future Bass, Pop, EDM, Lo-Fi, Acoustic.
- **5 MIDI stems, 15 variations each** — Piano Chords, Bassline, Lead Melody,
  Drums, Pluck — each with a per-stem semitone shift (-12…+12) and a piano-roll
  preview.
- **Instant drag & drop MIDI.** Every stem card writes a standard `.mid` file
  (960 PPQ, tempo meta-event) and hands it to the host's drag-and-drop.
- **IMPORT AUDIO.** Decode a `.wav/.aiff/.flac/.mp3/.ogg` file and feed it
  through the same analysis pipeline as live input.

---

## Project layout

```
MagicalChords/
├── CMakeLists.txt
├── README.md
├── LICENSE / THIRD_PARTY_NOTICES.md
├── .github/workflows/windows-vst3.yml   # CI: builds the VST3 on Windows
├── docs/{PRODUCT_SPEC.md, ROADMAP.md}
├── Tests/
│   └── test_dsp_cores.cpp               # standalone PitchDetector + KeyEstimator tests
└── Source/
    ├── PluginProcessor.{h,cpp}          # AudioProcessor shell + analysis wiring
    ├── PluginEditor.{h,cpp}             # Neon UI + chord/MIDI generation engine
    └── Analysis/
    │   ├── AudioAnalysisWorker.{h,cpp}  # Thread + FIFO + live waveform + key pipeline
    │   ├── PitchDetector.{h,cpp}        # YIN monophonic pitch estimator (pure C++)
    │   └── KeyEstimator.h               # Krumhansl-Schmuckler key finder (pure C++, header-only)
    ├── Harmony/ChordEngine.{h,cpp}      # Diatonic progression + 7th voicings (programmatic API)
    ├── Arrangement/ArrangementEngine.{h,cpp}  # Rule-based bass/lead/arp/drums (programmatic API)
    └── MIDI/MidiExportEngine.{h,cpp}    # Standard MIDI file writer (programmatic API)
```

> The editor uses its own richer vocal-aware generator (`buildMidiFile` in
> `PluginEditor.cpp`). `ChordEngine` / `ArrangementEngine` / `MidiExportEngine`
> are a parallel programmatic API for headless use and future tooling; they
> compile as part of the target so they stay available.

---

## Audio → MIDI pipeline

```
audio thread (processBlock)
   │  mono down-mix
   ▼
pushAudio()
   ├─► 256-sample live waveform ring (lock-free) ─► editor preview
   └─► AbstractFifo (262k samples) ─► worker thread
                                       │
                                       ▼
                          8192-sample window, 1024-sample hop
                                       │
                                       ▼
                          PitchDetector::detect()  (YIN + parabolic interp)
                                       │
                          confidence > 0.5?
                                       │ yes
                                       ▼
                          KeyEstimator.addPitchClass(pc, confidence)
                                       │
                          every 8 confident frames
                                       ▼
                          KeyEstimator::estimate()  (Krumhansl-Schmuckler)
                                       │
                          publish tonic + minor via std::atomic
                                       │
UI thread (30 Hz) ──► read atomics ──► applyDetectedKeyAndScale()
   │
   ▼
computeVocalPhrasePlan()  (chord degrees per bar, snapped to vocal pitch class)
   │
   ▼
buildMidiFile(cardIndex)  → juce::MidiMessageSequence → .mid file
   │
   ▼
DragAndDropContainer::performExternalDragDropOfFiles()  → drop into DAW
```

---

## Building

### Option A — GitHub Actions (no local compiler needed)

1. Push this folder to your GitHub repo `Magical-Chords-RAYAN-ZACK`.
2. The workflow `.github/workflows/windows-vst3.yml` clones JUCE 7.0.12,
   configures CMake and builds `MagicalChords_VST3` on `windows-2022`.
3. Go to the repo **Actions** tab → **Windows VST3 Build** → run it.
4. Download the `Magical-Chords-Windows-VST3` artifact from the run.
5. Copy `Magical Chords.vst3` to `C:\Program Files\Common Files\VST3\` and
   rescan plugins in your DAW.

### Option B — Local build (any OS)

Requirements: **JUCE 7+**, **CMake 3.22+**, a C++17 compiler.

```bash
cmake -B build -DJUCE_PATH=/path/to/JUCE
cmake --build build --config Release
```

The CMake auto-detects a `JUCE/` subfolder if present, so you can also drop the
JUCE source tree next to `CMakeLists.txt` and omit `-DJUCE_PATH`.

A `MagicalChords_Standalone` app is also built for testing without a DAW.

### Validate the DSP cores (no JUCE needed)

```bash
g++ -std=c++17 -O2 -I Source -o test_dsp_cores \
    Tests/test_dsp_cores.cpp Source/Analysis/PitchDetector.cpp
./test_dsp_cores
```

This compiles the production `PitchDetector.cpp` + `KeyEstimator.h` and checks
YIN pitch accuracy (220 Hz / 440 Hz sines, silence rejection) and key finding
(C-major → tonic C / major, A-minor → tonic A / minor), end-to-end.

---

## DAW routing for live vocal analysis

1. Put a vocal (live mic or recorded clip) on an audio track.
2. Load **Magical Chords** on that track as an **effect** (not an instrument).
3. Click **START LISTENING**. Audio passes through unmodified while the worker
   analyses it.
4. Watch the chord cards update as the vocal progresses; drag any stem into a
   MIDI track.

The plugin accepts audio and produces MIDI. To hear the generated MIDI, route a
soft-synth to receive MIDI from the Magical Chords track, or drag the `.mid`
files onto instrument tracks.

---

## What was completed in this pass

The repository shell compiled but the audio-analysis backend was unfinished and
the CI could not build. This version makes it a working, end-to-end plugin:

1. **`AudioAnalysisWorker` extended** with a lock-free 256-sample live waveform
   ring, Krumhansl–Schmuckler key estimation (via the new `KeyEstimator`), an
   async `reset()` (UI thread signals the worker, which clears its own state),
   and `detectedTonic()` / `detectedMinor()` / `copyLiveWaveform()` accessors.
2. **New `Source/Analysis/KeyEstimator.h`** — pure-C++, header-only,
   unit-testable Krumhansl–Schmuckler key finder (parallel to `PitchDetector`).
3. **`PitchDetector.cpp`** — fixed an out-of-bounds read in the parabolic peak
   interpolation when `bestLag == maxLag`.
4. **`PluginProcessor` stubs replaced** — `getDetectedTonic()`,
   `isDetectedMinor()`, `copyLiveWaveform()` and `resetAudioAnalysis()` now
   return real data instead of `0` / `false` / zeros. Added `loadAudioFile()`
   that decodes WAV/AIFF/FLAC/MP3/OGG and feeds it through the same DSP path.
5. **`PluginEditor.cpp` IMPORT AUDIO** now actually decodes the chosen file
   and runs `applyDetectedKeyAndScale()` so the progression updates from the
   imported audio (previously it only started live capture).
6. **`CMakeLists.txt`** now compiles all seven `.cpp` files (previously only
   `PluginProcessor.cpp` + `PluginEditor.cpp` were listed, which caused
   undefined-reference linker errors for the analysis backend).
7. **`.github/workflows/windows-vst3.yml`** now checks out JUCE 7.0.12 and
   passes `-DJUCE_PATH` to CMake (previously the workflow failed instantly
   with "JUCE not found").
8. Removed the stale root-level duplicate files (`AudioAnalysisWorker.h`,
   `PluginProcessor.*`, `PluginEditor (1).cpp`) that were not part of the
   `Source/` build tree.
9. Standalone tests (`Tests/test_dsp_cores.cpp`) validate the pitch and key
   cores against generated signals — all pass.

---

## Notes & future work

- The pitch detector is **monophonic** (single vocal line). Polyphonic input
  lowers confidence; route the plugin on a dedicated vocal bus for best
  results.
- `buildMidiFile()` is re-invoked from `paint()` for the five cards on every
  repaint (~30 Hz). It works but is write-heavy; cache the `juce::MidiFile`
  per card and rebuild only when inputs change if CPU becomes an issue.
- Chord / arrangement generation is deterministic (key, scale, genre,
  variation seed, vocal trail). There is no neural-net inference on the audio
  thread — "AI" refers to the adaptive key/phrase analysis and auto-pilot
  regeneration, which run cheaply in real time. A model-based generator can be
  dropped behind the same `computeVocalPhrasePlan` / `buildMidiFile` interface.
- See `docs/ROADMAP.md` for the full feature roadmap and what remains
  unimplemented (polyphonic transcription, piano-roll editing, installer/
  signing, validated per-host drag/drop).
