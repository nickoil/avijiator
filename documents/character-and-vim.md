# Character & "Vim" — Feature Spec

Everything here is an *addition* to the core voice defined in `architecture.md`.
The core prototype (PolyBLEP osc → SVF → ADSR) will sound correct but clean and
static. This document covers what closes the gap to an analog-modelled instrument
with performance feel.

**Design principle: everything in here is optional and switchable.** A clean,
un-coloured mode must remain reachable — both for A/B comparison while developing
and because a clinical version is sometimes the right sound.

---

## Control architecture

Three tiers of control, so you're not hunting through menus mid-set:

### Tier 1 — the VIM switch (single global toggle)
One master switch enabling a curated set of "always tasteful, never in the way"
colouration. These are the things you'd essentially never want to tweak per-patch,
just on or off:

- Analogue noise floor (~-70dB broadband hiss)
- Output soft-clip / saturation stage
- Asymmetric clipping (even-harmonic warmth)
- Oscillator drift / micro-instability
- Exponential (rather than linear) envelope curves
- Curved (rather than linear) velocity + mod-depth response

Implementation: a single `bool vimEnabled` gating a `CharacterProcessor` block,
plus curve-selection flags read by the envelope and mapping code. One switch on
the panel, no sub-parameters exposed.

### Tier 2 — dedicated sections (own UI, own knobs)
Things with a genuine "how much / what kind" dimension that you *will* want to
dial per-patch:

- Filter drive / feedback saturation amount
- Chorus / ensemble (on/off + rate + depth)
- Humanise (swing amount, timing jitter, velocity jitter)
- Per-note randomisation depth
- FM / ring-mod character amount

Each gets its own toggle *and* its own controls.

### Tier 3 — routing (matrix or per-destination switches)
- Mod wheel → destination + depth
- Aftertouch → destination + depth
- Velocity → filter / amp depth

---

## A. Analogue realism (the "Nord-esque" tier)

These address the gap between a mathematically clean digital synth and a
modelled analogue one.

### A1. Nonlinear filter — saturation in the feedback path
**The single highest-impact change.** A plain SVF is a *linear* filter: clean,
predictable, sterile. Real analogue filters soft-clip *inside* the resonance
feedback loop, so as resonance and input level rise the filter itself generates
harmonics and growls rather than just boosting a spectral peak.

- Insert a `tanh()` (or similar) soft clipper **inside** the filter feedback
  loop, not just on the output — placement is the whole point
- Adds nonlinear cutoff↔resonance interaction: cutoff drift under high
  resonance, asymmetric pole behaviour, more musical self-oscillation
- **Control**: Tier 2 — "Drive" knob. Amount matters per-patch.

### A2. Exponential envelope curves
Linear ADSR ramps sound mechanical. Real envelope circuits are
exponential/logarithmic.

- Exponential decay/release curves; consider per-stage curve shaping
  (fast initial attack curve, slower tail)
- Very high perceived-quality gain for very little code
- **Control**: Tier 1 (VIM switch). Optionally expose a curve amount later if
  you find you want it per-patch.

### A3. Oscillator drift / micro-instability
Real VCOs are never perfectly stable. A perfectly stable digital oscillator is
one of the clearest "tells" that something is synthetic.

- Slow random pitch wander, sub-cent to a couple of cents
- Independent per oscillator (main / sub) so they drift against each other
- **Control**: Tier 1 (VIM switch), depth fixed at a tasteful default.

### A4. Oversampling
- 2x or 4x oversample the oscillator + filter block
- Mainly matters at high resonance and bright settings, where the filter's own
  nonlinearity folds aliasing back into the audible range
- Do this *after* A1 — it's a fix for problems A1 introduces
- **Control**: settings-level (quality switch), not a performance control.
  Consider CPU impact on the Pixel target.

### A5. Component-bleed modelling
Real analogue circuits aren't perfectly isolated between stages.

- Slight nonlinear response curves on knob/CV mapping (not perfect 0–1 linear)
- Subtle cross-talk: e.g. envelope faintly affecting cutoff even when not routed,
  minor oscillator sync artefacts
- **Control**: Tier 1 (VIM switch). This is texture, not a parameter.

### A6. Curved velocity / mod-depth response
Linear 1:1 mappings feel flat. Perceived intensity isn't linear.

- Curve response so small deltas at low values are less audible than the same
  delta at high values
- **Control**: Tier 1 (VIM switch) for the default curve; Tier 3 for depth
  amounts per routing.

### Architectural caveat
Some of the Nord Modular's character is *architectural*, not a missing feature:
it's a patchable modular system, so cross-modulation between many modules gives
combinatorial complexity a fixed-architecture synth can't have by design. The
SH-101 is deliberately simpler and more direct — don't chase Nord complexity at
the cost of the SH-101's immediacy. Fix the realism gap (A1–A6); accept the
architecture gap.

---

## B. Performance feel (the "élan" tier)

These are about the instrument feeling alive *while played*, rather than
sounding rich in isolation.

### B1. Stereo width — chorus / ensemble
A mono voice is inherently flat in a mix.

- Multiple slightly-detuned delay taps, LFO-modulated — the Juno-style ensemble
  effect that makes a simple voice sound lush and wide
- Cheaper alternative: stereo unison — two detuned oscillator copies panned apart
- **Control**: Tier 2 — on/off + rate + depth.

### B2. Humanised timing on arp / sequencer
Perfectly quantised steps sound like a machine, because they are one.

- **Swing**: delay every other step by a settable amount
- **Timing jitter**: tiny random offset per step (a few ms)
- **Velocity jitter**: small random velocity variation per step
- Should be an *amount* control, not always-on — sometimes rigid is right
- **Control**: Tier 2 — "Humanise" section with swing + jitter amounts.

### B3. Per-note randomisation
No two triggers of an analogue envelope/filter are identical.

- Tiny per-trigger variance on cutoff, amp level, and pitch
- Distinct from A3 (which drifts continuously); this varies *per note event*
- **Control**: Tier 2 — depth knob.

### B4. Output stage colouration
- **Noise floor**: very quiet broadband hiss (~-70dB). Counterintuitive, but a
  perfectly silent digital noise floor reads as synthetic
- **Output saturation**: gentle tape/tube-style soft clip on the master bus —
  separate from A1's in-filter saturation. Glues transients, adds harmonics.
  This is what most "analogue warmth" plugins actually are
- **Asymmetric clipping**: real transistor/diode circuits clip positive and
  negative swings differently, producing even-harmonic content that reads as
  "warm" rather than "distorted"
- **Control**: Tier 1 (VIM switch) — all three, no exposed parameters.

### B5. Expressive real-time control
- **Mod wheel** → filter cutoff or LFO depth
- **Aftertouch** → vibrato or filter
- **Live knob response on held notes**: cutoff/resonance must respond
  *while a note sustains*, not just at note-on — sweeping a self-oscillating
  resonance by hand is a huge part of "playing" this kind of synth
- **Control**: Tier 3 — routing destinations + depths.

### B6. FM / ring-mod character (optional, beyond-SH-101)
A pure subtractive saw/square can't reach inharmonic bite.

- Small amount of self-FM, or sub-osc ring-modulating / FMing the main osc
- Adds edge and grit under leads and basses
- Explicitly *not* SH-101-authentic — treat as an extra mode, not a default
- **Control**: Tier 2 — on/off + amount.

---

## Implementation priority

Highest audible improvement per hour of work, in order:

1. **A1** — filter feedback saturation (biggest single "aliveness" win)
2. **A2** — exponential envelope curves (trivial code, large perceptual gain)
3. **B2** — swing / humanisation on the arp
4. **B1** — chorus / ensemble on the output
5. **B3** — per-note micro-randomisation
6. **A3** — oscillator drift
7. **B4** — output noise floor + saturation
8. **A4** — oversampling (only if you're hearing harshness at high resonance)
9. **B5** — mod wheel / aftertouch routing
10. **A5, A6** — bleed modelling, response curves (polish pass)
11. **B6** — FM / ring-mod (character extension, do last)

Items 3–5 hit "feels alive" perception more directly than further refining the
core oscillator/filter maths, and are small additions relative to the DSP work
already scoped.

---

## Testing notes

- **Always keep a bypass path.** Build the VIM switch early, even before most of
  what it gates exists — A/B against clean is how you tell whether a change is
  actually an improvement or just different.
- **Watch CPU on the Pixel target.** Oversampling (A4) and chorus (B1) are the
  two real CPU costs here. Test on-device, not on the Windows build, before
  committing to them for live use.
- Some of these are deliberately *below conscious perception* individually
  (A3, A5, B3, B4's noise floor). Evaluate them by toggling during sustained
  playing, not by soloing them.
