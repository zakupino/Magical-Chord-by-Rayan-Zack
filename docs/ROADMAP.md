# Development roadmap

## Phase 1 — Foundation
- [x] Create repository and initial project structure
- [x] Define product scope and visual direction
- [x] Add Windows x64 GitHub Actions build workflow
- [x] Confirm JUCE/CMake Windows x64 builds
- [ ] Verify the VST3 artifact in real DAW hosts

## Phase 2 — Plugin shell and UI
- [x] Branded charcoal/cyan interface
- [x] Manual key, scale and progression selectors
- [x] Genre, arrangement-part and tempo controls
- [x] Audio file import and waveform preview
- [ ] UI scaling, accessibility and host-size validation

## Phase 3 — Audio analysis
- [x] Live DAW audio capture and bounded SPSC handoff to worker thread
- [x] Initial YIN-style monophonic pitch estimator connected to live input
- [ ] Validate pitch accuracy against synthetic and recorded test signals
- [ ] Pitch-to-note event segmentation with note start/end times
- [ ] Onset/rhythm and tempo analysis
- [ ] Key/scale estimation with confidence and manual correction
- [ ] Polyphonic pitch/chord transcription
- [ ] Analyze imported audio instead of waveform preview only

## Phase 4 — Harmony and arrangement
- [x] Initial diatonic progression engine for major/natural minor
- [x] Seventh-chord voicings and starter progression presets
- [x] Rule-based Bassline, Lead, Arpeggio and Drums MIDI generators
- [x] Genre selector for Pop, EDM, Cinematic, Lo-fi and Hip-Hop
- [ ] Expand chord vocabulary (Maj9, Min9, Add9, Sus2/Sus4, inversions)
- [ ] Voice-leading, register constraints and musical quality tests
- [ ] More genre/mood/complexity controls and arrangement variations
- [ ] AI/ML inference pipeline and model evaluation (not implemented)

## Phase 5 — MIDI and DAW integration
- [x] Standard MIDI file export for chords and individual arrangement parts
- [x] Tempo metadata in exported MIDI
- [ ] Host tempo/transport sync exposed in the UI and used by exports
- [ ] Host-specific drag/drop validation; keep MIDI-file export fallback
- [ ] Piano-roll editing, audition and regenerate controls

## Phase 6 — Release quality
- [ ] Automated unit tests for pitch, harmony, arrangement and MIDI output
- [ ] CPU/memory and real-time safety review under stress
- [ ] FL Studio, Ableton Live, Cubase and Studio One compatibility tests
- [ ] State save/restore and preset persistence
- [ ] Installer, signing, versioning and release documentation
- [ ] Licensing review, including JUCE commercial terms

**Release note:** Current output is a beta VST3 foundation, not a completed AI product. An Actions build passing proves compilation and artifact packaging only; it does not prove musical accuracy or DAW compatibility.
