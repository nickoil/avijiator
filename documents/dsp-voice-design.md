# Voice DSP design — oscillator + filter core

Design record for **Stage A item 2** ([TODO.md](TODO.md)). Written before
implementation. Covers the four sound sources, the mixer, and the 24dB resonant
lowpass — everything from the VCO to the VCA in
[architecture.md](architecture.md)'s signal chain.

Envelope, LFO, glide and note handling are **not** covered here — items 3 and 4.
This document defines where they plug in, and nothing more.

---

## 1. Decisions and why

| Decision | Choice | Reasoning |
|---|---|---|
| SVF implementation | Hand-rolled TPT/Zavalishin | Full control of the feedback path so drive can be added later without a rewrite. Also avoids depending on JUCE API details we can't verify by reading sources. |
| Resonance topology | Global feedback loop; each 12dB stage fixed at `k=2` | Exact self-oscillation threshold, one clean peak, single insertion point for later saturation. |
| Oscillator structure | One phase accumulator, three taps | Mirrors a real SH-101 VCO (saw / pulse / sub-divider off one oscillator). Makes the sub phase-locked for free. |
| Sub-oscillator phase | Derived from main accumulator, not its own | An independent accumulator drifts against the saw and beats audibly over tens of seconds. |
| Pitch and cutoff storage | `log2(Hz)`, not Hz | Modulation sums linearly in octaves, and linear smoothing of log2 *is* multiplicative smoothing of frequency. |
| Filter drive / saturation | Not in item 2 | Keeps the SH-101 A/B honest — one variable at a time. Tracked as its own TODO. |

### Relationship to the open "SVF vs. ladder" decision

[architecture.md](architecture.md)'s open decisions list still has *"exact filter
topology (SVF cascade vs. ladder emulation)"* unresolved, and item 2 **does not
settle it**. Worth being precise about what was chosen, though:

The filter is built from **SVF stages we own** — that satisfies "SVF first, not a
ladder" as an implementation choice. But fixing each stage at `k=2` and putting
resonance in a global loop yields `1/(s+1)^4`, which is the same pole placement a
4-pole transistor ladder has. So the *response* is ladder-like even though the
*topology* is an SVF cascade.

This was chosen deliberately: it gives an exactly derivable self-oscillation
threshold and one obvious place to add nonlinearity. If it turns out to lack
character, the escape hatch is the drive stage, not a topology rewrite.

---

## 2. Oscillator — PolyBLEP

Naive saw/square are banned ([CLAUDE.md](../CLAUDE.md)) — aliasing is the whole
point. PolyBLEP corrects each waveform discontinuity with a 2-sample polynomial
approximation of a band-limited step.

### The BLEP residual

Phase `t` normalised to `[0, 1)`; `dt = f / fs` the normalised increment. The
residual is scaled for a step of amplitude 2 — which is both the saw's wrap
(+1 → −1) and the pulse's edges — so it's used unscaled for both:

```cpp
float polyBlep (float t, float dt) noexcept
{
    if (t < dt)          { t /= dt;             return t + t - t * t - 1.0f; }  // 2t - t^2 - 1
    if (t > 1.0f - dt)   { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }  // (t + 1)^2
    return 0.0f;
}
```

### Saw

One downward step at the wrap:

```cpp
saw = 2.0f * t - 1.0f - polyBlep (t, dt);
```

### Pulse with PWM — two corrections per cycle

A pulse of duty `w` has **two** discontinuities: a rising edge at `t = 0` and a
falling edge at `t = w`. PWM moves the second one. Each correction must be
evaluated in its own edge-relative phase:

```cpp
auto fallingPhase = t - w;
if (fallingPhase < 0.0f)
    fallingPhase += 1.0f;

pulse = (t < w ? 1.0f : -1.0f)
      + polyBlep (t, dt)                 // rising edge at 0
      - polyBlep (fallingPhase, dt);     // falling edge at w
```

Sanity check against the canonical 50% form (`+= blep(t); -= blep(fmod(t+0.5,1))`):
at `w = 0.5`, `(t − 0.5) mod 1 == (t + 0.5) mod 1`. Consistent.

**Dynamic duty clamp — the part that's easy to get wrong.** Each correction window
is `2*dt` wide (`dt` either side of the edge). If `w < 2*dt` or `1 − w < 2*dt` the
two windows overlap and the corrections corrupt each other. A static `[0.02, 0.98]`
clamp is *not* enough at high pitch — clamp against `dt`:

```cpp
const auto w = juce::jlimit (juce::jmax (minPulseWidth,        2.0f * dt),
                             juce::jmin (maxPulseWidth, 1.0f - 2.0f * dt),
                             pulseWidth);
```

Degrades toward square at high pitch, which is the right failure mode.

**Known and deliberately unfixed:** a pulse of duty `w` carries a DC component of
`2w − 1`. Real hardware AC-couples it away. Left in for now per "keep item 2
vanilla" — but when item 3 sweeps PWM with the LFO, the moving DC will thump.
Tracked as a TODO (DC blocker after the mixer).

### Sub-oscillator — shares the accumulator

A square one octave down, derived arithmetically via a divide-by-two flip-flop
toggled on each main-phase wrap:

```cpp
const auto subPhase = 0.5f * t + (subHigh ? 0.5f : 0.0f);   // exact half-rate, zero drift
const auto subDt    = 0.5f * dt;

auto subFallingPhase = subPhase + 0.5f;
if (subFallingPhase >= 1.0f)
    subFallingPhase -= 1.0f;

sub = (subPhase < 0.5f ? 1.0f : -1.0f)
    + polyBlep (subPhase,        subDt)
    - polyBlep (subFallingPhase, subDt);
```

Continuity trace: cycle N (`subHigh = false`) gives `subPhase ∈ [0, 0.5)`, sub = +1.
At the wrap `subPhase` passes 0.5⁻ → 0.5⁺ continuously; cycle N+1 gives
`subPhase ∈ [0.5, 1)`, sub = −1. At the next wrap it passes 1.0⁻ → 0. So `subPhase`
is a genuine half-rate ramp, both BLEP windows land on real edges, and the sub is
exactly one octave down **by construction**. 50% duty, so no DC.

### Precision and clamping

```cpp
static constexpr float  minFrequencyHz = 8.0f;
static constexpr double maxIncrement   = 0.25;   // fs/4, not Nyquist
```

`fs/4` because the sub runs at half the increment and the BLEP windows need room.
A 12 kHz ceiling is a non-issue for a bass synth.

`phase` and `phaseIncrement` stay `double`; BLEP math in `float`. Float phase
accumulation would add ~6e-8 jitter per add — inaudible, but doubles cost nothing
here.

---

## 3. Filter — TPT SVF, 24dB

### One 12dB stage (Simper form)

Prewarped integrator gain and derived coefficients:

```
g  = tan (pi * fc / fs)
a1 = 1 / (1 + g * (g + k))
a2 = g * a1
a3 = g * a2
```

Per-sample update with two trapezoidal integrator states:

```cpp
const auto v3 = input - ic2eq;
const auto v1 = c.a1 * ic1eq + c.a2 * v3;
const auto v2 = ic2eq + c.a2 * ic1eq + c.a3 * v3;

ic1eq = 2.0f * v1 - ic1eq;
ic2eq = 2.0f * v2 - ic2eq;

return v2;    // lowpass. bandpass = v1, highpass = input - k*v1 - v2
```

The stage owns **only** `ic1eq` / `ic2eq`. Coefficients are computed once per sample
in the `Vcf` and shared across both stages — one `tan()` per sample, not two.

### Why `k = 2` fixed per stage

Normalised, the SVF lowpass is `H(s) = 1 / (s² + k·s + 1)`. At `k = 2`:

```
H(s) = 1 / (s² + 2s + 1) = 1 / (s + 1)²
```

Two cascaded gives `1 / (s + 1)⁴` — four coincident real poles, identical to a
four-stage ladder's core, built from stages we control.

### Self-oscillation threshold — exactly 4

Evaluate the cascade at the cutoff, `s = j`:

```
(j + 1)^4 = ((j + 1)^2)^2 = (2j)^2 = -4
=>  H(j) = -1/4
```

Magnitude ¼, phase exactly 180° — which turns *negative* feedback into *positive*
feedback at the cutoff. Loop gain is `k_res / 4`, so unity (self-oscillation, exactly
at the cutoff) occurs at:

```
k_res = 4
```

This is why the Moog ladder's resonance control famously runs 0–4.

> **A linear filter does not self-oscillate — it diverges.** `k_res = 4` is where
> oscillation *starts*, but sustaining it at a steady amplitude requires a
> nonlinearity. In a real ladder that is transistor saturation. In a purely linear
> digital model the poles cross into the right half plane and the output grows
> exponentially → `inf` → `NaN`, and NaN integrator states latch permanently: the
> synth goes silent and no control recovers it.
>
> **Found by ear at step 6** — resonance to full killed the voice with no way back.
> The fix is `softClip` on the feedback path in `Vcf` (below), which bounds the loop
> so oscillation settles into a limit cycle. It is *exactly* linear below its
> threshold, so the signal stays vanilla at normal levels.
>
> This is a **stability requirement, not flavour** — distinct from the voiced
> drive/saturation TODO, which is a driven stage meant to colour the sound at all
> levels. The original plan deferred saturation as cosmetic; that was wrong.

Mapping:

```cpp
static constexpr float maxFeedback = 4.5f;   // BY EAR — not derived
const auto k = resonance01 * maxFeedback;
```

The overshoot past 4.0 exists because discrete-time pole placement isn't infinitely
exact; it puts the top of the knob firmly on the oscillating side rather than sitting
on the threshold.

### Zero-delay feedback solve

Each stage's lowpass output is **affine** in its input:

```
v2 = ic2eq + a2*ic1eq + a3*(in - ic2eq)
   = a3*in + [(1 - a3)*ic2eq + a2*ic1eq]
   = G*in + S
```

So the cascade is affine too. Closing the loop `u = x − k·y` and solving directly:

```
y * (1 + k*Gt) = Gt*x + St
y = (Gt*x + St) / (1 + k*Gt)      where Gt = G1*G2,  St = G2*S1 + S2
```

The denominator is `>= 1` for `k >= 0`, so the solve is always well-defined. **No
unit delay in the feedback path** — therefore no cutoff-dependent resonance
detuning and no delay-induced blowup. (The loop can still genuinely self-oscillate
when `k` passes the physical threshold — that's the intent, not an artefact.)

Then run the stages forward with the solved loop input so the integrator states
advance consistently, and return stage 2's output rather than `solved`.

The loop input is soft-clipped — `softClip (in − k·solved)` — which is what bounds
self-oscillation (see the box above):

```cpp
if (x >  threshold) return  threshold + tanh (x - threshold);
if (x < -threshold) return -threshold + tanh (x + threshold);
return x;                                   // exactly linear inside
```

Below the threshold the forward pass reproduces `solved` to within rounding; above
it the two legitimately differ, which is the whole point — so there is no
solved-vs-result equality assert. The invariant that actually matters is
**finiteness**, since one non-finite sample latches the states forever.

`SynthVoice::renderNextBlock` also carries a per-block non-finite scan that resets
the filter and clears the block if it ever trips. That should be unreachable now the
loop is bounded, but "silent until the app is restarted" is an unacceptable failure
mode for an instrument intended to be played live, so it recovers rather than merely
asserting.

### Resonance level compensation

Feedback costs passband level: DC gain falls to `1/(1+k)`. Analog ladders lose bass
exactly the same way, so compensate only **partially**:

```cpp
in *= (1.0f + resonanceCompensation * k);   // resonanceCompensation = 0.5f, BY EAR
```

At 0 you get full analog-style bass loss at high resonance; at 1 the level holds flat
and sounds thin and clinical.

### Numerical notes

- **Denormals.** Integrator states decaying toward zero are the classic trap —
  hundreds of cycles per sample. `juce::ScopedNoDenormals` at the top of the audio
  callback sets FTZ/DAZ. The −120 dBFS noise floor below also keeps states above
  denormal range.
- **`tan()` near Nyquist.** Clamp cutoff to `[20 Hz, min(18 kHz, 0.45*fs)]`. At
  `0.45*fs`, `g = tan(81°) ≈ 6.3` — comfortable.
- **Self-oscillation from silence.** A perfectly zero input into a perfectly zero
  state stays zero forever. Real analog starts from thermal noise, so a −120 dBFS
  noise floor is injected into the filter.
- **Cost.** One `tan()` + one `exp2()` per sample ≈ 100k transcendental calls/sec at
  48 kHz. Negligible. Do **not** pre-optimise into a lookup table until a profile says
  otherwise.

---

## 4. Parameter plumbing

Nine `std::atomic<float>` in a `VoiceParameters` struct, `std::memory_order_relaxed`
on load and store. Relaxed is *correct* here, not a shortcut: these are independent
scalars with no cross-parameter consistency requirement. Guarded with
`static_assert (std::atomic<float>::is_always_lock_free, ...)`.

Required by [CLAUDE.md](../CLAUDE.md): UI → audio goes through atomics, never a shared
mutable field.

### Pattern: snapshot per block, smooth per sample

- Reading atomics **per sample** lets a knob step mid-block → click.
- Reading **per block without smoothing** steps at every block boundary → zipper.
- So: snapshot atomics once at the top of `renderNextBlock`, step smoothers once per
  sample.

`snapshotParameters (bool jumpImmediately)` is called with `true` from `prepare()` so
the first block doesn't ramp up from zero, `false` every block after.

### Smoothing

| Parameter | Domain | Ramp |
|---|---|---|
| `pitchLog2Hz` | log2 Hz, linear smoother | 20 ms |
| `cutoffLog2Hz` | log2 Hz, linear smoother | 20 ms |
| `sawLevel`, `pulseLevel`, `subLevel`, `noiseLevel` | linear | 20 ms |
| `pulseWidth` | linear | 20 ms |
| `resonance` | linear | **50 ms** — a resonance jump moves the whole feedback loop and thumps |
| `outputLevel` | linear | 20 ms |

**Linear smoothing on a log2 value, not `ValueSmoothingTypes::Multiplicative`.**
Linear-in-log2 *is* multiplicative in Hz, gives the musically correct octave-per-second
sweep, and sidesteps Multiplicative's strictly-positive-value divide-by-zero hazard.
One smoother type everywhere.

Nothing in item 2 is discrete. When items 3/6 add waveform and routing switches, those
get a separate `std::atomic<int>` read per block with **no** smoother.

---

## 5. Modulation summing points (for items 3 and 4)

Three named zero-valued locals in the per-sample loop, so item 3 is a three-line diff
rather than a restructure:

```cpp
const auto pitchModulationOctaves  = 0.0f;  // item 3: LFO -> pitch;  item 4: glide
const auto cutoffModulationOctaves = 0.0f;  // item 3: env*envToCutoff + lfo*lfoToCutoff
const auto amplitudeModulation     = 1.0f;  // item 3: env when routed to VCA; item 7: accent
```

The asymmetry is deliberate: pitch and cutoff mods are **additive in octaves**,
amplitude mod is **multiplicative and unity-defaulted**. That matches how the
modulators actually behave, so item 3 never has to reason about what "zero" means in
each place.

Cutoff sums **two** sources (envelope and LFO). Octaves, not Hz — a modulator that
moves cutoff by a fixed number of Hz sounds completely different at 200 Hz and at
5 kHz. `exp2` happens once, inside the `Vcf`, *after* the sum. Never bake a raw Hz
value into the `tan()`.

---

## 6. Structure

```
Source/DSP/
  VoiceParameters.h          struct of std::atomic<float>          (header only)
  PolyBlepOscillator.h/.cpp  VCO: saw + pulse + sub, one accumulator
  NoiseGenerator.h           xorshift32 white noise                (header only)
  TptSvfStage.h              one 12dB TPT SVF, integrator states   (header only)
  Vcf.h/.cpp                 24dB cascade + global resonance loop
  Vca.h                      output amplifier                      (header only)
  SynthVoice.h/.cpp          voice core — wires the above together
```

Small per-sample kernels are header-only so they inline in Debug builds too, where
LTO isn't helping.

### `SynthVoice` is the reuse boundary

[architecture.md](architecture.md) requires one voice core shared by the arpeggiator
and step-sequencer front ends. That's enforced by the interface, not by convention:

```cpp
void prepare (double sampleRate);
void reset() noexcept;
void renderNextBlock (float* output, int numSamples) noexcept;   // writes, does not add
VoiceParameters& getParameters() noexcept;
```

It knows nothing about MIDI, timers, tempo, patterns, or `juce::AudioBuffer`. It takes
numbers and produces samples.

**The raw-pointer signature is the important part.** It's already sub-block splittable,
so item 5's sample-accurate arp clock calls it several times per block with advancing
pointers, inserting `noteOn()` between calls — with **no signature change**. That is
the single most important structural decision for items 4, 5 and 7.

Item 4 adds `noteOn`/`noteOff`/glide to `SynthVoice`. Items 5 and 7 become peer classes
that own zero DSP and only call those methods at sample offsets.

### Debug slider scaffolding — throwaway

Item 2 has no note input (that's item 4), so the voice drones continuously and is
auditioned with nine plain sliders: **Pitch, Saw, Pulse, Pulse width, Sub, Noise,
Cutoff, Resonance, Level** — Pulse width sits next to Pulse rather than after Noise,
since the two are always adjusted together when auditioning.

These are deliberately unstyled and explicitly disposable — **item 6 is the real UI
pass and none of this survives it**. To keep them from sprawling, they're driven by a
static spec table (`name, min, max, default, storeAsLog2, pointer-to-member`) and one
construction loop, so the whole thing is ~40 lines of obviously-deletable code rather
than 200 lines of copy-paste.

Hz-valued sliders get `setSkewFactorFromMidPoint (sqrt (min * max))` and store
`log2(value)`. Each slider's `onValueChange` is invoked once during construction to
seed the atomics from the table, so the sliders and the voice can't disagree at
startup.

---

## 7. Build order

Each step ends in an app that **builds, runs and makes sound**, so it can be run and
listened to between any two steps, and stopped at any of them.

### How to run a step

```
Build Saw from documents/dsp-voice-design.md
```

That is the whole instruction — this document carries everything a fresh session
needs. Step names and numbers are interchangeable ("build step 2" works too). At the
end of each step I build it, confirm it compiles and launches, and stop for a
listening test rather than continuing.

**Sessions cluster by file — don't `/clear` between every step.** Steps 2–4 are all
the same oscillator file and the same BLEP math; steps 5–6 are both the filter, and 6
builds directly on 5's code. Clearing inside a cluster throws away context that is
immediately needed again, and costs a re-read of this document each time.

| Session | Steps | Model | Shares |
|---|---|---|---|
| A | **Plumbing** (1) | Sonnet | `SynthVoice` + `MainComponent` |
| B | **Saw, Pulse, Mixer** (2–4) | Opus | All `PolyBlepOscillator.cpp` |
| C | **Filter, Resonance** (5–6) | Opus | Both `Vcf.cpp` |
| D | **Polish** (7) | Sonnet | Docs, guard, tick the box |

`/model` switches model without clearing, so the Sonnet→Opus change is not a reason to
start a new session.

The real trigger for `/clear` is not "a step finished" — it is "the context is now
mostly things that no longer matter": build logs, superseded attempts, resolved
debugging. When that happens mid-cluster, `/compact` is the better tool;
[CLAUDE.md](../CLAUDE.md) already defines what it should keep and drop.

[CLAUDE.md](../CLAUDE.md)'s model policy puts item 2 on Opus, but not every sub-step
earns it — steps 1 and 7 are plumbing and polish, so Sonnet saves the tighter Opus
sub-limit for the steps where a wrong answer is silent.

Each step has a one-word name — **"build Resonance"** is as good an instruction as
"build step 6".

| # | Name | Model | Work | Ends with |
|---|---|---|---|---|
| **0** | **Design** | Sonnet | This document, the `architecture.md` link, and the two new TODO entries. **No code.** | Design captured before implementation |
| 1 | **Plumbing** | Sonnet | `VoiceParameters.h` + `SynthVoice` skeleton (naive saw) + `MainComponent` rewiring: `ScopedNoDenormals`, mono render, channel fan-out, prepare/reset. **Plus `Vca.h`, the Level slider, and the amplitude mod summing point.** | Same audible result as item 1, new plumbing proven — and it can be turned down |
| 2 | **Saw** | **Opus** | `PolyBlepOscillator`, **saw only**. Pitch + Saw sliders, their smoothers, pitch mod point. | Band-limited saw. **Run the aliasing sweep here**, before anything can mask it |
| 3 | **Pulse** | **Opus** | Pulse + PWM. Pulse + Pulse width sliders. | Verify the dynamic duty clamp degrades to square at high pitch rather than glitching |
| 4 | **Mixer** | **Opus** | Sub (derived phase + flip-flop) + `NoiseGenerator`. Sub + Noise sliders. | Mixer complete. Verify the sub is one octave down with no drift |
| 5 | **Filter** | **Opus** | `TptSvfStage.h` + `Vcf` with **resonance forced to 0**. Cutoff slider, cutoff mod point. | Plain 24dB lowpass — confirms the TPT math before the loop can mask a coefficient bug |
| 6 | **Resonance** | **Opus** | ZDF global feedback solve, resonance mapping, compensation, −120 dBFS noise floor. Resonance slider. | **Self-oscillation test.** Highest-risk step, deliberately not merged into 5 |
| 7 | **Polish** | Sonnet | `#if JUCE_DEBUG` NaN/range guard; comment pass; reconcile this document with anything that changed; tick TODO item 2. | Item 2 closed, nine sliders live |

### Before you `/clear`

Context does not survive `/clear` — this document does. So **any decision made by ear
mid-session must be written back into §8 before clearing**, or it is lost. If
`maxFeedback` ends up at 5.2 because 4.5 self-oscillated too early, that number needs
to be in the table, not just in the code.

**The Level slider lands in step 1, not at the end.** The voice drones continuously
from step 1 onward, so a working volume control is needed for all six subsequent
rounds of listening — not delivered after them.

**Steps 2, 5 and 6 are where a wrong answer is *silent*** — it sounds slightly off
rather than failing outright. Each is isolated so a problem can be attributed to the
step that introduced it, rather than debugging a 300-line big-bang.

---

## 8. Tuning constants — set by ear, not derived

These are starting values. All of them are listening decisions:

| Constant | Start | What it does |
|---|---|---|
| `maxFeedback` | `4.5f` | Where on the resonance knob self-oscillation kicks in |
| `resonanceCompensation` | `0.5f` | How much bass loss remains at high resonance |
| `softClipThreshold` | `1.0f` | Self-oscillation amplitude, and how hard the filter clips when driven. Lower = earlier, more compressed oscillation |
| Smoothing ramps | 20 ms / 50 ms | Laggy vs. still-zippering |
| `minPulseWidth` / `maxPulseWidth` | `0.02` / `0.98` | PWM travel at the extremes |

---

## 9. What can and cannot be claimed

Per [CLAUDE.md](../CLAUDE.md): *"Builds and runs" is a claim you can make. "Sounds
correct" is not.*

**Objective, claimable:** builds clean in Debug and Release with no new warnings;
launches and stays up; a `#if JUCE_DEBUG` per-block guard asserting
`isfinite` and `|sample| < 32.0f` holds while every knob is swept to both extremes.

**Objective but needs a human at the speakers:**
- *Self-oscillation* — all four levels at 0, resonance max, sweep cutoff. Must produce
  a clean sine tracking the cutoff. "Non-silent with all sources at zero" is a fact,
  not a taste judgement.
- *Sub lock* — saw + sub up, hold a minute. Any slow beating means the phase derivation
  is wrong.
- *Aliasing* — saw only, cutoff open, sweep pitch slowly upward. Naive gives obvious
  downward-moving birdie tones against the upward sweep; PolyBLEP gives none. Listening
  for a specific artefact's presence or absence, not for whether it sounds good.

**Taste — the user's call alone:** whether it's "in the family" with an SH-101 (the
actual acceptance criterion), whether the resonance taper feels right, whether the
bass-loss compensation is set well, whether smoothing feels laggy, whether PWM extremes
sound right.
