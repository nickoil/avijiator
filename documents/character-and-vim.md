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

---

## V1 build scope — settled (session of 2026-09-03)

The full spec above is ~19 cells of controls (5 toggles, 14 knobs/choices).
Checked against `PanelSection::widthForCells` and the existing SEQUENCER/
OUTPUT row's actual layout (`SynthPanel.cpp`'s `resized()`): that row uses
744 of its 1212px budget today (SEQUENCER 452 + gap 16 + OUTPUT 276), leaving
468px slack. A third section slotted between them costs one more 16px gap,
so its own budget is 436px ≈ **4 cells, max**, before `designWidth` would
need to grow — a real constraint, not a rough guess.

**User's call: a curated v1 that fits in that slack, zero canvas growth.**
Cut to the doc's own top 3 priority items (A1, A2, B1, B2 - implementation
priority list above, items 1-4) plus the pairing trick ARP/SEQ's own On/Hold
and On/Record cells already establish:

| Cell | Contents | Priority items covered |
|---|---|---|
| 1 (ToggleStack) | VIM (top), Chorus On (bottom) | Tier 1 gate; B1 |
| 2 (knob) | Drive | A1 |
| 3 (knob) | Humanise | B2 |

**3 cells (264px), not 4** - leaves 172px of the row's slack still spare for
a later Tier-2 addition (Chorus Rate/Depth, Randomise) without redoing this
layout math. New `PanelSection characterSection { "CHARACTER" }`, matching
the existing all-caps section-title convention, inserted between
`seqControlSection` and `outputSection` in `resized()`'s row-C block (and
in the constructor's own section-visibility loop).

**Deliberate scope cuts from the full spec, to fit 3 cells - revisit later,
not forgotten:**

- **Drive is knob-only, no separate toggle** - `filterDriveAmount`
  defaulting to 0 (inert) is the same "0 = no effect" convention every other
  depth knob in `VoiceParameters` already uses (`envToCutoffDepthOctaves`,
  `lfoToPitchDepthOctaves`, ...), not a new pattern. Deviates from Tier 2's
  general "each gets its own toggle" line in favour of matching this
  codebase's existing convention.
- **Chorus ships with fixed internal rate/depth, no exposed knobs yet** -
  just the on/off toggle. B1's own Rate/Depth knobs are deferred; add them
  to the section later (172px of spare slack already budgeted for exactly
  this) once a fixed default has been lived with.
- **Humanise is ONE knob, not three** - B2 lists Swing, Timing Jitter, and
  Velocity Jitter as separate amounts; v1 combines them behind a single
  `humaniseAmount` (0-1) that scales all three by fixed internal ratios
  (implementation's own call exactly which ratios - not fixed here). 0 =
  fully quantised (today's exact behaviour, unchanged). Splitting back into
  3 knobs later just needs 2 more cells, well inside the remaining slack.
- **A2 (exponential envelope curves) rides on the VIM toggle as originally
  spec'd** - no separate control, no separate cell; `vimEnabled` gates it
  directly in the ADSR's curve shaping.
- **Everything else deferred entirely, not gated by anything built here**:
  noise floor, output saturation, asymmetric clipping, oscillator drift, A5/
  A6 (component-bleed, curved response), per-note randomisation (B3), mod
  wheel/aftertouch routing (B5 - no MIDI CC/aftertouch plumbing exists yet),
  the two already-built-but-unexposed velocity knobs
  (`velocityToAmpDepth`/`velocityToCutoffDepthOctaves`), and FM/ring-mod
  (B6, lowest priority, explicitly non-authentic). None of these are gated
  by `vimEnabled` yet even where the full spec says they should be
  eventually - `vimEnabled` only drives A2 for now, so the switch does
  exactly what it visibly does, nothing latent.

**New DSP touch points for v1** (Sonnet-level per this doc's own model
note above - not item 2/5's DSP invention, but real signal-path code,
so build and verify each in isolation before wiring the panel):

- `Source/DSP/Vcf.cpp` - a `tanh()` (or equivalent) soft clip INSIDE the
  resonance feedback loop (A1's own placement note: not on the output),
  scaled by `filterDriveAmount`. At 0, must be byte-identical to today's
  output - same "inert at default" proof every additive modulation term in
  this codebase already carries.
- `Source/DSP/Adsr.cpp` - exponential decay/release curve shaping, gated by
  `vimEnabled`. Off must be byte-identical to today's linear ramps.
- `Source/Arpeggiator.cpp` / `Source/StepSequencer.cpp` - swing + timing
  jitter + velocity jitter derived from one `humaniseAmount` atomic, applied
  wherever each already computes a step's timing/velocity. 0 must be
  byte-identical to today (already sample-accurate, unswung, unjittered).
- New `Source/DSP/Chorus.h/.cpp` - a small multi-tap modulated-delay block
  at the output stage, gated by `chorusEnabled`, fixed internal rate/depth
  constants for v1 (see the scope-cut above).
- `Source/DSP/VoiceParameters.h` - new atomics: `vimEnabled` (int),
  `filterDriveAmount` (float, 0 default), `humaniseAmount` (float, 0
  default), `chorusEnabled` (int, 0 default). All four get picked up by
  `SynthPanel::forEachSerializableParameter` automatically once given
  `KnobSpec`/`ToggleSpec` entries - no separate preset-serializer change
  needed (documents/settings-persistence-design.md section 4's whole point).

**Next session prompt**: `Build Character & Vim v1 from documents/character-and-vim.md`.

---

## V1 build record (session of 2026-09-03)

Built as scoped above. New/changed files:

- `Source/DSP/VoiceParameters.h` — four new atomics: `vimEnabled`,
  `filterDriveAmount`, `humaniseAmount`, `chorusEnabled`.
- `Source/DSP/Vcf.h/.cpp` — `driveSaturate(x, driveAmount)` pushes the
  *existing* feedback-loop `softClip` harder via a caller-supplied gain, then
  divides it back out; `processSample` gained a fourth `driveAmount`
  parameter. At `driveAmount == 0`, `driveGain == 1.0f` exactly, so this is
  `softClip(x) / 1.0f` — byte-identical to before A1 existed.
- `Source/DSP/Adsr.h/.cpp` — `setCurveEnabled(bool)`; Decay/Release each grew
  an exponential branch (time-constant = stage seconds / 5, ~99% arrival by
  the nominal time) alongside the original linear one. Attack stays linear in
  both modes, per this doc's own v1 scope note. Off (default) takes the
  original branch untouched.
- `Source/DSP/Humanise.h` (new) — the pure swing/jitter/velocity-jitter
  formulas, shared by the arp and the sequencer the same way `StepClock.h`'s
  `beatsPerStepForDivision` is shared, since both consumers need the exact
  same arithmetic. Swing takes its odd/even parity from `StepClock::
  getStepIndex()`, exactly as that method's own comment anticipated.
- `Source/Arpeggiator.h/.cpp`, `Source/StepSequencer.h/.cpp` — a THIRD
  scheduling deadline (`noteOnPending`/`samplesUntilNoteOn`), the same
  "independent countdown, take the min" shape each already used for its
  gate-off deadline. A step boundary now *schedules* a note-on
  `Humanise::onsetDelaySamples` samples in the future instead of always
  firing immediately; at `humaniseAmount == 0` that delay is always exactly
  0, so the immediate-fire branch is byte-identical to before B2. Both
  `reset()` and `releaseVoice()` clear the pending state, so a hand-over
  mid-delay drops the note rather than firing it late — this needed an actual
  fix (see below), not just a comment. `StepSequencer` additionally needed to
  clear the OLD note's `gateIsOpen` bookkeeping before scheduling a slide's
  delayed onset, or the previous note's own stale gate-off deadline could
  fire mid-delay and cut the slide short.
- `Source/DSP/Chorus.h/.cpp` (new) — hand-rolled two-tap modulated delay
  (fixed ~0.6Hz LFO, ~15ms base delay, ±5ms depth, 50/50 wet), left/right
  reading the LFO π apart. `MainComponent::getNextAudioBlock` calls it once
  per sample, replacing the mono→stereo copy, only while `chorusEnabled` is
  on.
- `Source/UI/SynthPanel.h/.cpp` — new `CHARACTER` section (3 cells: VIM+
  Chorus toggle stack, Drive, Humanise) between SEQUENCER and OUTPUT, wired
  through the existing `KnobSpec`/`ToggleSpec` machinery so preset save/load
  and `refreshControlsFromParameters` cover it for free.

**One real defect found by self-test, not by ear**: the first draft of both
`Arpeggiator::process` and `StepSequencer::process` asserted a scheduled
onset was audible one block before it could possibly have fired (the
scheduling block itself, before the onset's own delay had elapsed) — a
self-test-authoring mistake, not a DSP one, caught immediately by `cdb.exe`
per this project's headless-assertion convention rather than surfacing later
as a mysteriously-early note.

Seven new Debug self-tests (`runAdsrCurveSelfTest`, `runVcfDriveSelfTest`,
`runChorusSelfTest`, `runHumaniseFormulaSelfTest`, `runArpHumaniseSelfTest`,
`runSeqHumaniseSelfTest`) plus the six pre-existing ones — 0 assertion hits
via `cdb.exe`. Builds clean (Debug + Release, zero warnings); both configs
launch and stay up with no crash.

**Not verified this session**: whether any of this sounds right — CLAUDE.md's
"what you cannot verify" section reserves that for the user, and it applies
here more than most items (this whole doc is about a listening-test-driven
character). MIDI hardware end-to-end also untested (none available).

---

## Rest of Tier 1 — build record (session of 2026-09-05)

Same session, later on: the user asked what VIM currently does, then how much
work the rest of Tier 1 was, then said to build it. Two design questions came
up along the way and were put to the user rather than guessed at:

1. **A3's sub-oscillator drift.** `PolyBlepOscillator.cpp` derives the
   sub-oscillator's phase arithmetically from the main phase ON PURPOSE — a
   second free-running accumulator was explicitly rejected there (its own
   comment: it "would slowly slip phase against the saw, audible as slow
   beating over tens of seconds"). A3 asks for main and sub to "drift against
   each other", which naively conflicts with that invariant. **Settled**: sub
   gets its own independently-seeded `OscillatorDrift` instance, but applied
   as a small, BOUNDED, slowly-wandering PHASE OFFSET added into the derived
   sub phase — not a second frequency/accumulator. `OscillatorDrift` is a
   leaky integrator (bounded by construction, decays back toward 0), not a
   free-running one, so it cannot reintroduce the rejected failure mode; it
   can only wobble within its own bounded range. Chosen over the two
   alternatives offered (sub tracks main's drift value; skip sub drift
   entirely) as the one that actually matches the spec's "against each
   other" without the risk.
2. **A5 (component-bleed).** Left deferred, per the user's choice — the doc's
   own examples ("envelope faintly affecting cutoff even when not routed")
   arguably contradict existing, deliberate behaviour (Envelope Destination =
   Amp means the envelope does not touch the filter, full stop), and the doc
   itself calls this item "texture, not a parameter" rather than a crisp
   spec. Revisit with a real design pass, not a guess.

**Built**, all gated by the SAME `vimEnabled` atomic A2 already reads — no
new controls, per Tier 1's own "one switch, no sub-parameters" design:

- **A3 — oscillator drift.** New `Source/DSP/OscillatorDrift.h/.cpp`: a
  one-pole leaky integrator fed by white noise (`state = state*decayCoeff +
  noise*stepGain`), producing a bounded, unscaled value nominally in roughly
  [-1, 1] regardless of sample rate (`stepGain` is solved so the walk's
  steady-state variance is 1). Two independently-seeded instances:
  - **Main** — `SynthVoice` owns one, scaled to ±2 cents (`maxMainDriftOctaves`)
    and summed into the existing pitch-modulation-in-octaves point, exactly
    like every other pitch modulator there (LFO, glide). Always advances
    (`processSample()` called every sample regardless of `vimEnabled`, same
    "always draw, zero contribution when off" posture the arp/seq's humanise
    generators already established); the CONTRIBUTION is exactly `0.0f` when
    off, not merely small.
  - **Sub** — lives inside `PolyBlepOscillator` itself (`setDriftEnabled`,
    mirroring `Adsr::setCurveEnabled`'s shape), scaled to a ±0.01-cycle
    bounded phase offset added directly into the arithmetically-derived
    `subPhase` before the PolyBLEP edge calculations, wrapped back into
    [0, 1). See design question 1 above for why this shape and not a second
    accumulator.
- **A6 — curved velocity response.** `curvedVelocity()` in the new
  `Source/DSP/CharacterProcessor.h` — `velocity^2`, chosen for exact endpoint
  preservation (0→0, 1→1, so a full-velocity note is unaffected either way)
  while satisfying the doc's "small deltas at low values are less audible"
  shape. Scope deliberately narrowed to velocity's own response curve only —
  NOT also a curve on the modulation-DEPTH knobs (envelope/LFO depth), which
  the doc's wording could also be read as asking for; that reading felt like
  it would double up with A2's already-curved envelope shape and with future
  knob-taper decisions, so it was left out rather than guessed at. `SynthVoice`
  computes `effectiveVelocity` once per block (velocity is constant across a
  block) and uses it everywhere `currentVelocity` used to feed the amp/cutoff
  summing points.
- **B4 — noise floor + output saturation + asymmetric clipping.** One class,
  `CharacterProcessor`, bundling all three (per B4's own "Tier 1 — all three,
  no exposed parameters" note) — NOT the same mechanism as `Vcf`'s existing
  `softClip` (that one lives inside the filter's feedback loop and is a
  stability requirement, not flavour). `-70dBFS` broadband hiss, then an
  asymmetric `tanh(x*drive)/drive` shaper (different `drive` for positive vs.
  negative swings — real transistor/diode asymmetry, even-harmonic "warmth"
  rather than a symmetric clipper's odd-harmonic-only spectrum; both branches
  have small-signal gain exactly 1, so normal-level signal passes through
  untouched and only peaks saturate). Owned by `MainComponent`, applied to
  the mono mix right after `renderVoiceBlock` and BEFORE `Chorus` — matches a
  real analogue chain's order (glue/saturation first, stereo widening after).

Four new Debug self-tests (`runOscillatorDriftSelfTest`,
`runCharacterProcessorSelfTest`, `runVimCharacterSelfTest` — the last one an
integration proof through `SynthVoice::renderNextBlock` that `vimEnabled ==
false` still renders byte-identical across independent runs and `true`
genuinely changes the output) plus the seven from the first build-record
entry and the six pre-existing ones — **18 total**, 0 assertion hits via
`cdb.exe`. Builds clean (Debug + Release, zero warnings); Release launched
and stayed up with no crash.

**Not verified this session**: same caveat as above, doubly so now — drift,
the velocity curve, and the noise floor/saturation are all explicitly
*sub-conscious* per the doc's own testing notes ("evaluate them by toggling
during sustained playing, not by soloing them"), which makes them the
hardest items in this whole doc to judge by a quick listen. MIDI hardware
still untested.
