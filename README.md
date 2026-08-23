# Avijiator

A monophonic subtractive synth inspired by the Roland SH-101 and TB-303, built
in JUCE. Classic VCO → VCF → VCA signal chain, single shared ADSR (mirroring
the SH-101's shared-envelope quirk), an arpeggiator first with a step
sequencer (accent/slide, acid-style) to follow.

**Author**: Nick Casey ([nickoil@hotmail.com](mailto:nickoil@hotmail.com))
**Kick-off date**: 2026-08-19

## Status

_As of 2026-08-23_ — Tooling is fully set up: JUCE 9.0.1 pinned as a git
submodule, CMake + VS2022 Build Tools, static (`/MT`) runtime linking. In
Stage A (Windows dev), the environment check and the full oscillator + filter
core are built and running — PolyBLEP saw/pulse (with PWM) + sub-oscillator +
noise, mixed into a hand-rolled 24dB resonant SVF lowpass with
self-oscillation. Design and build order in
[documents/dsp-voice-design.md](documents/dsp-voice-design.md).

Next up: envelope + LFO (item 3). See [documents/TODO.md](documents/TODO.md)
for the live checklist — this section will drift out of date as work
continues, the checklist won't.

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
- [documents/character-and-vim.md](documents/character-and-vim.md) —
  analogue realism / performance-feel layer spec (item 8, later)
