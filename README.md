# Avijiator

A monophonic subtractive synth inspired by the Roland SH-101 and TB-303, built
in JUCE. Classic VCO → VCF → VCA signal chain, single shared ADSR (mirroring
the SH-101's shared-envelope quirk), an arpeggiator first with a step
sequencer (accent/slide, acid-style) to follow.

**Author**: Nick Casey ([nickoil@hotmail.com](mailto:nickoil@hotmail.com))
**Kick-off date**: 2026-08-19

## Status

Early planning stage — no code yet. See [documents/TODO.md](documents/TODO.md)
for the current checklist.

## Platform plan

Developed first as a **Windows standalone app** (VS Code + CMake) for fast
build/debug iteration, then ported to **Android** (Pixel) as a standalone
live-performance app once the core voice/arpeggiator is solid. Full reasoning
and build order in [documents/architecture.md](documents/architecture.md).

## Docs

- [documents/architecture.md](documents/architecture.md) — signal chain, voice
  architecture, platform/tooling decisions, live rig hardware
- [documents/TODO.md](documents/TODO.md) — actionable build checklist
