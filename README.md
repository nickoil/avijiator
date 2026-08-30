# Avijiator

A monophonic subtractive synth inspired by the Roland SH-101 and TB-303, built
in JUCE. Classic VCO → VCF → VCA signal chain, single shared ADSR (mirroring
the SH-101's shared-envelope quirk), an arpeggiator and a 16-step sequencer
(accent/slide, acid-style).

**Author**: Nick Casey ([nickoil@hotmail.com](mailto:nickoil@hotmail.com))
**Kick-off date**: 2026-08-19

## Status

_As of 2026-08-30_ — Stage A (Windows dev) core is built end-to-end: PolyBLEP
saw/pulse (with PWM) + sub-oscillator + noise → resonant 24dB SVF lowpass with
self-oscillation, shared ADSR (filter/amp/both) + LFO (triangle/square/S&H),
mono note-priority handling across QWERTY/on-screen/MIDI input, an
arpeggiator with a sample-accurate clock, a 16-step sequencer (per-step
pitch/gate/accent/slide plus two filter lanes, with live pitch recording),
and a full mouse-driven UI panel for all of it. See
[documents/TODO.md](documents/TODO.md) for the live checklist — this section
will drift out of date as work continues, the checklist won't.

Next up: settings persistence / presets (item 8), then internal tempo sync
for LFO/arp/glide.

## Platform plan

Developed first as a **Windows standalone app** (VS Code + CMake) for fast
build/debug iteration, then ported to **Android** (Pixel) as a standalone
live-performance app once the core voice/arpeggiator is solid. Full reasoning and build order in [documents/architecture.md](documents/architecture.md).

## Docs

- [documents/architecture.md](documents/architecture.md) — signal chain, voice
  architecture, platform/tooling decisions, live rig hardware
- [documents/TODO.md](documents/TODO.md) — actionable build checklist
- [documents/dsp-voice-design.md](documents/dsp-voice-design.md) — oscillator
  + filter DSP design and build order (item 2)
- [documents/envelope-lfo-design.md](documents/envelope-lfo-design.md) —
  shared ADSR + LFO design and build order (item 3)
- [documents/note-handling-design.md](documents/note-handling-design.md) —
  mono note-priority, glide/legato, multi-input design (item 4)
- [documents/arpeggiator-design.md](documents/arpeggiator-design.md) —
  arpeggiator clock, pattern modes, hold/latch design (item 5)
- [documents/ui-design.md](documents/ui-design.md) — instrument panel layout
  and control-binding design (item 6)
- [documents/step-sequencer-design.md](documents/step-sequencer-design.md) —
  16-step sequencer design and build order (item 7)
- [documents/settings-persistence-design.md](documents/settings-persistence-design.md)
  — save/reload + presets design (item 8, in progress)
- [documents/tempo-sync-design.md](documents/tempo-sync-design.md) — shared
  master tempo for LFO/arp/glide (not yet started)
- [documents/character-and-vim.md](documents/character-and-vim.md) —
  analogue realism / performance-feel layer spec (item 9, later)
- [documents/step-automation.md](documents/step-automation.md) — generalised
  per-step parameter automation ("p-locks"), a future consideration beyond
  item 7's fixed lanes
- [documents/future-work.md](documents/future-work.md) — larger ideas not yet
  scoped into a numbered item (VST3 conversion, a drum voice)
