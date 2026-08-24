# SH-101-a-like — Architecture

A monophonic subtractive synth (SH-101 / TB-303 inspired) built in JUCE. Developed
first as a **Windows standalone app** for fast iteration, then ported to Android
(Pixel) as a standalone live-performance app.

## Platform decision

- **Framework**: JUCE (C++), compiled as a **standalone app** — no DAW, no VST host needed on stage
- **Dev platform (first)**: Windows standalone app — same machine as the editor,
  build/run cycle is seconds instead of a phone deploy, real keyboard/mouse for
  quick UI iteration, and a debugger actually attaches. All core DSP (oscillator,
  filter, envelope, arp) gets built and validated here before touching Android.
- **Target OS (port, second)**: Android, built via JUCE's Android export →
  Android Studio project, once the voice/arp core is solid on Windows
- **Build machine**: Windows throughout (Android Studio + JUCE, no Mac required
  for either stage)
- **iOS**: ruled out for this project — no Mac access, and free-tier Apple signing
  requires re-signing every 7 days, which doesn't fit a "build once, gig with it"
  workflow. Revisit only if a Mac + $79/yr Developer account becomes available.

## Signal chain

```
VCO (saw / square+PWM / sub-osc / noise mix)
        │
        ▼
VCF (24dB resonant lowpass, self-oscillating at high resonance)
        │
        ▼
VCA
        │
        ▼
Output
```

Modulation:
- **Envelope (single ADSR)**, switchable/routable to filter, amp, or both
  (mirrors the real SH-101's shared-envelope quirk — deliberate constraint, not a bug)
- **LFO** (triangle / square / random) → pitch and/or filter cutoff
- **Portamento/glide** on pitch, with legato vs. retrigger mode switch

The envelope and LFO are specified in detail — ADSR state machine, the gate
edge-detection pattern, LFO waveform math, routing, and parameter smoothing — in
[envelope-lfo-design.md](envelope-lfo-design.md).

## Voice architecture (core, shared across both note-input modes)

- **Monophonic** — single voice, no polyphony
- **Oscillator**: PolyBLEP-based saw and square/pulse (avoids naive aliasing),
  plus a sub-oscillator (square, one octave down) and a white noise generator,
  mixed before the filter
- **Filter**: state-variable filter (SVF) cascade or ladder-style emulation,
  resonant lowpass, self-oscillates at max resonance
- **Envelope**: one ADSR generator, routable to VCF/VCA
- **Note handling**: mono note-priority logic (last-note or highest-note priority),
  glide/legato behavior on overlapping notes

This voice is the shared core for both the arp and step-sequencer front ends —
same oscillator/filter/envelope code, different things trigger it.

The oscillator and filter are specified in detail — PolyBLEP math, TPT SVF
coefficients, the resonance/self-oscillation derivation, parameter smoothing and
the modulation insertion points — in
[dsp-voice-design.md](dsp-voice-design.md).

This core voice is deliberately **clean**: correct DSP, no analogue realism or
performance-feel colouration layered on yet. That layer — filter feedback
saturation, oscillator drift, humanised timing, output noise floor, chorus, and
more, all gated behind one global switch so a clinical A/B reference stays
reachable — is a separate later addition, not part of the core. Full spec in
[character-and-vim.md](character-and-vim.md).

## Note input: arpeggiator vs. step sequencer

Both considered; **decision: build the arpeggiator first**, add step sequencer later
as a second input mode once the voice is solid.

| | Arpeggiator | Step sequencer |
|---|---|---|
| Input | Hold a chord, auto-cycles through held notes | Pre-programmed steps (pitch/gate/accent/slide) |
| Character | Reactive, improvisable, live | Fixed, repeatable, precise |
| Needed for | General playability | Classic acid basslines (accent/slide only make sense here) |
| Build effort | Lower | Higher (needs step-edit UI, pattern storage) |
| Build order | **Phase 1** | **Phase 2** |

### Arpeggiator logic
- Maintain a sorted buffer of currently-held notes (update on note-on/note-off)
- Clock ticks (host-independent here, since this runs standalone — internal
  tempo control instead of DAW sync)
- On each subdivision, advance through the buffer per selected pattern
  (up / down / up-down / random / as-played), trigger the mono voice
- Sample-accurate step timing: don't rely on a coarse `Timer` — schedule
  note-on/off against exact sample positions within each audio block

### Step sequencer logic (phase 2)
- Fixed-length pattern (e.g. 16 steps, page-able for longer patterns)
- Per step: pitch, gate on/off, **accent** flag, **slide** flag
- Accent: boosts velocity → pushes envelope amount / filter cutoff / volume
  harder on that step (core of the acid sound)
- Slide: consecutive slide-flagged steps glide pitch instead of retriggering
  the envelope
- Same clock/trigger plumbing as the arp underneath

## UI

- Panel-style layout echoing the SH-101 (knobs for cutoff, resonance, envelope
  ADSR, LFO rate/depth, glide time) — familiar layout over redesigned layout
- Step grid for sequencer mode (tap to toggle step, secondary gesture for
  accent/slide — no right-click on touch, use long-press or a mode toggle)
- Touch-first design (not a shrunk desktop UI) since Android is the primary target

## Live performance rig

```
Pixel phone (USB-C)
   │
   ▼
Powered USB-C hub
   ├── USB audio interface  → line/balanced out → mixer / PA
   └── USB MIDI keyboard (or Bluetooth MIDI, skips the hub)
```

### Hardware

- **Phone**: existing Pixel 6, or secondhand Pixel 6/7a/8a (Tensor-generation
  AAudio/Oboe low-latency support is consistent across this range — no need to
  buy new; Pixel 10a is the value pick *if* buying new, but secondhand is cheaper
  and sufficient)
- **Audio interface**: bus-powered USB-C, class-compliant, e.g. Focusrite
  Scarlett Solo (4th Gen) — reliable Android USB support, modest power draw
- **Hub**: must be a genuinely *powered* hub, not a passive splitter — running
  an audio interface + MIDI keyboard off one phone port is where cheap hubs
  brown out
- **MIDI**: USB class-compliant keyboard, or Bluetooth MIDI if avoiding the hub
  entirely (trade-off: Bluetooth adds latency/dropout risk, avoid for tightly
  timed playing)

### Software audio path

**Windows (dev phase)**
- Use JUCE's built-in **ASIO** support where available (audio interface with an
  ASIO driver, or ASIO4ALL as a fallback) for low-latency output; WASAPI
  exclusive mode is an acceptable fallback if no ASIO driver is present
- Latency isn't the primary concern here — this phase is about correctness of
  the DSP and UI, not final round-trip numbers. Don't over-invest in tuning
  Windows audio settings; that effort belongs in the Android phase

**Android (port phase)**
- Use **AAudio low-latency (MMAP) path** explicitly in JUCE's Android audio
  settings — not automatic, must be configured
- Target round-trip latency ~10–20ms
- Test on the actual physical device early — don't assume desktop-level
  responsiveness; build a minimal "tap = click sound" JUCE test app first to
  sanity-check latency before porting the full synth

## Build order (recommended)

### Stage A — Windows (dev)

1. **Environment check**: minimal JUCE Windows standalone app, confirm it
   builds, runs, and makes sound
2. **Oscillator + filter core**: saw/square + sub + noise mix → resonant
   lowpass, confirm it's "in the family" against reference SH-101 recordings
3. **Envelope + LFO**: shared ADSR routing, LFO → pitch/filter
4. **Mono note handling**: glide, legato/retrigger modes (drive with a USB
   MIDI keyboard or on-screen/computer-keyboard note input on Windows)
5. **Arpeggiator**: pattern modes, sample-accurate clock, rate control
6. **UI pass**: knobs + controls for what's built so far (mouse-driven for
   now; touch-first layout decisions still apply, just not touch-tested yet)
7. **Step sequencer** (phase 2): step grid, accent/slide, pattern storage
8. **Character & "Vim"**: analogue realism + performance-feel layer on top of
   the clean core voice — see [character-and-vim.md](character-and-vim.md)
   for the full spec, control tiering, and priority order

### Stage B — Android (port)

8. **Port setup**: JUCE Android export → Android Studio project, confirm the
   Stage A code builds, installs, and makes sound with acceptable latency on
   the Pixel — expect audio-path and touch-input adjustments here, not DSP
   rewrites
9. **Touch pass**: adapt the UI from mouse to touch (tap-to-toggle steps,
   long-press/mode-toggle for accent/slide per the UI section above)
10. **Field test**: full rig (phone + hub + interface + MIDI) under real
    playing conditions before relying on it at a gig

## Open decisions / things to verify hands-on

- Exact filter topology (SVF cascade vs. ladder emulation) — start with SVF,
  it's easier to get right, revisit if it doesn't have enough character.
  **Still open.** Item 2 builds it from SVF stages, but fixes each stage at
  critical damping and puts resonance in a global feedback loop, which gives
  ladder-equivalent pole placement (`1/(s+1)^4`). Topology is SVF; response is
  ladder-like. If it lacks character the first move is the drive stage, not a
  topology rewrite — see [dsp-voice-design.md](dsp-voice-design.md)
- Note-priority scheme for the mono voice (last-note vs. highest-note) — try
  both, keep whichever feels right by ear
- Whether the interface + hub + phone power chain is actually stable under
  load — this is a "test it, don't assume it" item
- How much the Windows→Android port actually costs once Stage A is done —
  JUCE should make the DSP/audio-callback code portable as-is, but UI
  input handling (mouse vs. touch) and the audio path (ASIO/WASAPI vs.
  AAudio) are known points that need real rework, not just a recompile
