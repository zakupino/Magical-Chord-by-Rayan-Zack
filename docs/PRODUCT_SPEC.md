# Product specification — MAGICAL CHORDS by RAYAN ZACK

## Product goal

A professional Windows 64-bit VST3 harmony and MIDI arrangement assistant that turns a sung, recorded, or played melodic idea into an editable, coordinated arrangement.

## Primary workflow

1. Capture or import a vocal/melody phrase.
2. Analyze pitch, note events, onset/rhythm, key/scale and tempo.
3. Present analysis with confidence indicators and allow manual correction.
4. Generate a chord progression and coordinated arrangement layers.
5. Audition, edit and regenerate individual layers.
6. Export each layer as a MIDI file; provide host drag-and-drop only where the host supports it reliably.

## Arrangement layers

- Piano / chord progression
- Bassline
- Lead melody
- Arpeggio
- Drums / rhythm

## Musical intelligence

Support expressive voicings (Maj7, Min7, Min9, Add9, Sus2, Sus4, etc.), sensible inversions, smooth voice-leading, register-aware voicing and genre/mood-aware variation. Generated parts should remain editable and deterministic when a seed or variation is held constant.

## Genre direction

Future Bass, Pop, Cinematic, Lo-Fi, Hip-Hop, EDM, Rock, R&B, Jazz and Ambient.

## Main interface

- Product identity: MAGICAL CHORDS by RAYAN ZACK
- Genre selector
- DAW Sync control
- Key selector with detected-key confidence and manual override
- BPM display with host tempo when available
- Prominent LISTEN / capture control
- Waveform and analysis timeline
- Chord progression timeline
- Compact layer cards for chords, bass, lead, arp and drums
- Preset/voicing variations, note editing and individual MIDI export

## Visual identity

Approved direction: premium charcoal/obsidian background with restrained cyan and teal-blue accents, following the second approved concept image. Keep contrast and typography comfortable; avoid excessive neon, glow and visual clutter.

## Technical requirements

- C++17 or newer and JUCE.
- Windows x64 VST3 as the first release target.
- Keep the audio callback real-time safe: no blocking I/O, allocations where avoidable, model inference or heavy analysis on the audio thread.
- Run analysis and inference asynchronously.
- Read host tempo, transport and time signature where available.
- Host key metadata is not consistently available; provide detection and manual override.
- MIDI file export is the dependable interoperability path. Drag-and-drop MIDI support must be validated per host.
- Include graceful behavior when no AI model or network connection is available.

## Quality bar

Musical results, predictable DAW behavior, low CPU use, clear error states, editable output, accessibility and repeatable host testing are release requirements. AI-generated output must never be presented as guaranteed musically correct.


## Implemented beta snapshot (2026-11-18)

The current repository implements the following subset of the target workflow:
- Live DAW audio input can be enabled and a monophonic pitch estimate is displayed when confidence is sufficient.
- Audio files can be imported and displayed as a waveform preview; imported-file transcription is not implemented.
- Manual key and scale selection drives the chord progression engine.
- A small set of deterministic progression presets and seventh-chord voicings are available.
- Rule-based bass, lead, arpeggio and drum MIDI parts can be exported individually, as can the chord progression.
- Tempo is manually selected for MIDI export.

The following product requirements remain unimplemented: vocal note transcription, polyphonic analysis, automatic key/tempo/onset detection, AI/model inference, advanced voicing and voice-leading, audio stem rendering, internal part audition, piano-roll editing, persistent presets/state, validated DAW drag/drop, and installer/signing. Do not advertise these as available features until they are implemented and tested.
