# Envelope + LFO design

Design record for **Stage A item 3** ([TODO.md](TODO.md)). Written before
implementation, same discipline as [dsp-voice-design.md](dsp-voice-design.md) for
item 2.

Item 2 is complete and closed — four sources through a 24dB resonant lowpass,
confirmed self-oscillating and stable. `SynthVoice.cpp` already carries three
modulation summing points, deliberately left at zero, explicitly commented as
item 3's insertion points:

```cpp
const auto amplitudeModulation     = 1.0f;  // item 3: env when routed to VCA
const auto pitchModulationOctaves  = 0.0f;  // item 3: LFO -> pitch
const auto cutoffModulationOctaves = 0.0f;  // item 3: env*envToCutoff + lfo*lfoToCutoff
```

Item 3's job is to build the shared ADSR and the LFO, and fill those three points
in — no restructuring of `SynthVoice` should be needed, only additions.

Per [CLAUDE.md](../CLAUDE.md)'s hard constraint: **one shared ADSR**, routable to
VCF, VCA, or both — mirrors the real SH-101's shared-envelope quirk, deliberate,
not a second envelope. Per [architecture.md](architecture.md): LFO is
triangle/square/random, routable to pitch and/or cutoff.

> **Model note**: unlike item 2, item 3 is not on CLAUDE.md's Opus list (only
> items 2 and 5 are named). Every step below defaults to **Sonnet**, following
> that rule literally. It still involves real DSP (envelope/LFO timing, retrigger
> behaviour) where a wrong answer can be silent — if Opus is wanted on the
> ADSR/LFO core steps (2-4) instead, that's a deliberate override of this default,
> not a correction to it.

---

## 1. Decisions and why

| Decision | Choice | Reasoning |
|---|---|---|
| ADSR implementation | Hand-rolled, not `juce::ADSR` | Matches the established pattern (hand-rolled PolyBLEP osc, hand-rolled TPT SVF): full control, and no need to verify JUCE retrigger/thread-safety behaviour by reading library sources |
| LFO "random" mode | Sample-and-hold | New random value each LFO cycle, held flat until the next — classic stepped/glitchy character, simplest to implement |
| Default envelope destination | Amp | Safest first-load behaviour: voice goes silent when not gated, rather than continuing to drone like it does today. Starting pick, easy to change by ear |
| Envelope curve shape | Linear, not exponential | [character-and-vim.md](character-and-vim.md) A2 already frames exponential curves as a later Tier-1 "Vim" addition on top of a clean core — item 3 stays clean, matching the project's own stated layering |

---

## 2. ADSR design

```cpp
enum class Stage { Idle, Attack, Decay, Sustain, Release };

void noteOn() noexcept;   // -> Attack, from wherever currentLevel currently is
void noteOff() noexcept;  // -> Release, from wherever currentLevel currently is
float processSample() noexcept;  // advances one sample, returns 0..1
```

**Rate-calibrated-to-full-range convention**, so every stage is click-free on
retrigger with no special-casing: each stage's increment is sized to cross the
*entire* 0↔1 range in the stage's nominal time —
`attackIncrement = 1 / (attackSeconds * sampleRate)`, likewise for decay/release.
If a stage starts partway (e.g. retriggering Attack from `currentLevel = 0.6`), it
proportionally finishes faster — the click-free behaviour falls out for free,
nothing extra to implement. Sustain holds `currentLevel = sustainLevel` until
`noteOff()`.

`reset()` (called from `SynthVoice::reset()`) returns to `Idle`, `currentLevel = 0`.

### The gate — and the one place this is easy to get wrong

Item 3 has no real note input yet (that's item 4), so a temporary **Gate button**
in the debug UI drives the envelope for testing. The tempting-but-wrong approach
is calling `envelope.noteOn()` / `noteOff()` directly from the button's message-thread
callback — that's an unsynchronized write into audio-thread state, exactly the
"shared mutable field" [CLAUDE.md](../CLAUDE.md) forbids.

**Correct pattern**: the button writes a plain `std::atomic<bool> gate` in
`VoiceParameters` (message thread, always safe). `SynthVoice` reads it once per
block and detects the edge **itself, from the audio thread**:

```cpp
// SynthVoice.h: private, audio-thread-only
bool lastGateState = false;

// SynthVoice.cpp, top of renderNextBlock, after snapshotParameters
const auto gateNow = parameters.gate.load (std::memory_order_relaxed);
if (gateNow != lastGateState)
{
    if (gateNow) envelope.noteOn(); else envelope.noteOff();
    lastGateState = gateNow;
}
```

No new method on `SynthVoice` is needed — `MainComponent` writes the atomic exactly
like every other debug control, just via a button instead of a slider.

Item 4 replaces this gate with real `noteOn(pitch, velocity)`/`noteOff()` driven by
MIDI/keyboard input; the edge-detection pattern here is what that will build on.

---

## 3. LFO design

```cpp
enum class Waveform { Triangle, Square, SampleAndHold };

void setRate (float rateHz) noexcept;
void setWaveform (Waveform w) noexcept;
float processSample() noexcept;  // bipolar, -1..1
```

One phase accumulator (`phase`/`phaseIncrement`, same style as the oscillator, no
BLEP — LFO rates are sub-audio, aliasing doesn't apply at the ranges this covers).

- **Triangle**: `phase < 0.5 ? (-1 + 4*phase) : (3 - 4*phase)` — symmetric ramp,
  -1 at phase 0, +1 at phase 0.5, back to -1 at wrap.
- **Square**: `phase < 0.5 ? 1.0f : -1.0f`.
- **Sample-and-hold**: draw a new random value from an owned `NoiseGenerator`
  (own seed, distinct from the audible noise source and the filter's floor noise)
  exactly at each phase wrap, hold it constant until the next wrap.

`reset()` returns `phase = 0`, held S&H value to `0`.

---

## 4. Routing

```cpp
enum class EnvelopeDestination : int { Filter = 0, Amp = 1, Both = 2 };
```

Stored as `std::atomic<int> envelopeDestination` in `VoiceParameters` — discrete,
no smoother, per the convention `dsp-voice-design.md` section 4 already
anticipated for this exact case. `LfoWaveform` likewise as `std::atomic<int>`.

Read once per block (not per sample, matching every other discrete control):

```cpp
const auto envDestination = (EnvelopeDestination) parameters.envelopeDestination.load (std::memory_order_relaxed);
const auto routeEnvToFilter = envDestination == EnvelopeDestination::Filter || envDestination == EnvelopeDestination::Both;
const auto routeEnvToAmp    = envDestination == EnvelopeDestination::Amp    || envDestination == EnvelopeDestination::Both;
lfo.setWaveform ((Lfo::Waveform) parameters.lfoWaveform.load (std::memory_order_relaxed));
```

Filling the three summing points — each source's `processSample()`/`getNextValue()`
called exactly **once** per sample and stored locally, then reused, so pitch and
cutoff don't desync the LFO or envelope by pulling two different samples of it:

```cpp
const auto envValue = envelope.processSample();          // 0..1
const auto lfoValue = lfo.processSample();                // -1..1

const auto pitchModulationOctaves = lfoValue * lfoToPitchDepthSmoothed.getNextValue();

const auto cutoffModulationOctaves =
    (routeEnvToFilter ? envValue * envToCutoffDepthSmoothed.getNextValue() : 0.0f)
    + lfoValue * lfoToCutoffDepthSmoothed.getNextValue();

const auto amplitudeModulation = routeEnvToAmp ? envValue : 1.0f;
```

Note the asymmetry this preserves from item 2's design: pitch/cutoff are additive
in octaves, amplitude is multiplicative and unity when not routed there — exactly
what the existing comments already promised.

---

## 5. Parameter plumbing — a refinement to the smoothing rule

`dsp-voice-design.md` section 4 established: smooth anything whose change would
otherwise cause an audible **value jump**; discrete switches read raw. Item 3 adds
a parameter class that rule didn't previously need to distinguish: **time
constants**. Changing `attackSeconds` mid-attack doesn't jump `currentLevel` — it
only changes the *rate* of future samples, which is inaudible as a "kink," not a
click. So:

| Parameter | Smoothed? | Why |
|---|---|---|
| `attackSeconds` / `decaySeconds` / `releaseSeconds` | **No** | Time constant only — changes future rate, not current output value |
| `sustainLevel` | **Yes** (20ms) | Directly assigned as `currentLevel` during the Sustain stage — a real value jump if unsmoothed |
| `lfoRateHz` | **No** | Same time-constant reasoning as the ADSR times |
| `envToCutoffDepthOctaves` / `lfoToPitchDepthOctaves` / `lfoToCutoffDepthOctaves` | **Yes** (20ms) | Directly scales a modulation amount every sample — a depth jump is an audible pop |
| `envelopeDestination` / `lfoWaveform` (enums) | No (discrete) | Existing convention |
| `gate` (bool) | No (edge-detected) | A trigger, not a continuous value |

This distinction — smoothed only when a parameter is *directly assigned as an
output value*, not merely a rate constant — generalises past item 3; item 5's arp
rate will likely follow the same "time constant, no smoothing" pattern.

---

## 6. Structure

New files in `Source/DSP/`, alongside the existing oscillator/filter files:

```
Source/DSP/
  Adsr.h/.cpp   new — envelope state machine
  Lfo.h/.cpp    new — triangle/square/S&H, owns its own NoiseGenerator
```

Both get `.cpp` files (not header-only) — same tier as `PolyBlepOscillator` and
`Vcf`: real setup logic and tuning constants, not a trivial one-line kernel like
`Vca.h`/`TptSvfStage.h`.

### MainComponent changes

Ten new debug rows plus one button:

- Attack, Decay, Sustain, Release (4 sliders, plain float targets)
- Env Destination (combo box — Filter/Amp/Both)
- Env → Cutoff Depth (slider)
- LFO Rate, LFO Waveform (combo — Triangle/Square/S&H)
- LFO → Pitch Depth, LFO → Cutoff Depth (sliders)
- **Gate** (`juce::TextButton`, press/release — see the JUCE-API flag below)

The two combo boxes don't fit the existing `DebugControlSpec` table (which is
`atomic<float>` + `Slider` only). Add a small parallel table rather than
complicating the existing one:

```cpp
struct DebugChoiceSpec
{
    const char* name;
    const char* const* choices;  // plain array + count, not juce::StringArray -
    int numChoices;              // simpler as a static-storage-duration table
    int defaultIndex;
    std::atomic<int> VoiceParameters::* target;
};
```

(Built as `const char* const*` + count rather than the `juce::StringArray` first
sketched here — a plain array is simpler to declare as a `static const` table at
namespace scope, matching `debugControlSpecs`' existing style.)

with its own tiny array of `{ juce::ComboBox, juce::Label }` and construction loop,
mirroring the float table's loop but wiring `comboBox.onChange` instead of
`slider.onValueChange`.

**Implementation gotcha**: `juce::ComboBox` item IDs are 1-based — 0 is reserved to
mean "no selection." So the wiring stores `choice index + 1` via `addItem`, and
subtracts 1 back off when reading `getSelectedId()`. Easy to miss, would otherwise
off-by-one every enum value.

The Gate button writes directly:
`voice.getParameters().gate.store (isDown, std::memory_order_relaxed);` — no
reflection table needed for one button.

**Window resize required.** Nine existing rows plus eleven new controls won't fit
600×400 — grow the window (built as 640×720) as part of step 1/4, not deferred to
Polish.

### CMakeLists.txt

Two new `target_sources` entries: `Source/DSP/Adsr.cpp`, `Source/DSP/Lfo.cpp`.
No other changes — `target_include_directories` and the module set already cover
this.

---

## 7. Build order

Unlike item 2, there's no Sonnet→Opus boundary here (see the model note above), so
clustering guidance is about context size only — the whole item could reasonably
run in one or two sessions rather than three.

| # | Name | Work | Ends with |
|---|---|---|---|
| **0** | **Design** | This document, `architecture.md` link, `TODO.md` link. **No code.** | Design captured before implementation |
| 1 | **Gate** | `VoiceParameters.gate` + Gate button + audio-thread edge detection in `SynthVoice`. Temporarily gates `amplitudeModulation` as a hard 0/1 (no ADSR yet) — proves the gate plumbing before real envelope shaping lands. Window resize. | Voice audibly mutes/unmutes on button press — gate plumbing proven |
| 2 | **Envelope** | `Adsr.h/.cpp`. Real attack/decay/sustain/release, routed to **Amp only** for now (simplest test — the classic pluck). Attack/Decay/Sustain/Release sliders. | Gate button now produces a real envelope shape, not a hard on/off — the "pluck" test |
| 3 | **Routing** | Envelope Destination combo (Filter/Amp/Both) + Env→Cutoff Depth slider. Wires the envelope half of the cutoff summing point. | Envelope audibly sweeps the filter when routed there — try all three destinations |
| 4 | **LFO** | `Lfo.h/.cpp`. Rate + Waveform combo + both depth sliders. Wires the pitch summing point and the LFO half of the cutoff summing point. | Vibrato (LFO→pitch) and filter wobble (LFO→cutoff) both audible; S&H should sound stepped, not smooth |
| 5 | **Polish** | Comment pass; reconcile this document with anything that changed; tick TODO item 3. | Item 3 closed |

**Steps 2 and 4 are where a wrong answer is silent** — retrigger clicks, S&H
sounding smooth instead of stepped, LFO desyncing between pitch and cutoff if
`processSample()` is accidentally called twice. Test each in isolation before
moving on.

### How to run a step

```
Build Envelope from documents/envelope-lfo-design.md
```

Same instruction pattern as item 2 — step names and numbers are interchangeable.

---

## 8. Verification

### Claimable without ears

- Builds clean (Debug + Release, zero new warnings).
- Launches, all eleven new controls present, no crash, no stderr.
- Gate button press/release doesn't leave the voice stuck (i.e. release always
  reaches Idle eventually, doesn't hang in Release forever) — checkable by
  inspecting `Adsr`'s stage in the debugger after several seconds of release.

### Needs a human at the speakers

- **Retrigger click test** — hold Gate, release partway through Decay, press again
  quickly. Should NOT click or jump; the rate-calibrated design should make this
  click-free by construction, but only ears confirm it.
- **S&H character** — LFO waveform = Sample & Hold, route to cutoff, low rate.
  Should sound stepped/glitchy, not a smooth wobble (that would indicate the
  Triangle/Square code path leaked in, or smoothing was accidentally applied to
  the LFO output itself).
- **Envelope destination** — cycle through Filter/Amp/Both with the same gate
  rhythm, confirm each sounds like what it says (amp = volume swells, filter =
  brightness swells, both = combined).
- **Pitch LFO desync check** — LFO→Pitch depth up, LFO→Cutoff depth up
  simultaneously, waveform Triangle. Both should visibly/audibly move in lockstep
  (same phase) — if they don't, `processSample()` is being called twice per sample
  somewhere.

### Taste — the user's call alone

Attack/decay/release feel, whether the default Amp destination is the right
starting point, whether LFO rate range covers what's actually useful, whether the
S&H character is "right" for this instrument.

---

## 9. JUCE APIs — flagged, then resolved

- **Gate button press/release**: `juce::Button::onStateChange` was flagged here as
  moderate-confidence, unverified. Rather than gamble on it, the build went
  straight to the unambiguous fallback: `GateButton` subclasses `juce::TextButton`
  and overrides `mouseDown`/`mouseUp` directly, chaining to the base class first so
  the button's own visual state keeps working. `onStateChange` was never tried.
- **`juce::ComboBox`** (`addItem`, `setSelectedId`, `getSelectedId`, `onChange`) —
  high confidence going in, confirmed correct. No surprises, see the 1-based ID
  gotcha in section 6.

---

## 9a. Actual values chosen — starting points, not derived

Mirrors `dsp-voice-design.md` section 8's convention: these are what the debug
sliders were built with, all by-ear starting points, not derived constants.

| Parameter | Range | Default |
|---|---|---|
| Attack / Decay / Release | 0.001s – 5.0s | 10ms / 100ms / 300ms |
| Sustain | 0 – 1 | 0.7 |
| Env → Cutoff Depth | 0 – 8 octaves | 0 (off) |
| LFO Rate | 0.02 Hz – 20 Hz | 2.0 Hz |
| LFO → Pitch Depth | 0 – 1 octave | 0 (off) |
| LFO → Cutoff Depth | 0 – 8 octaves | 0 (off) |

---

## 10. Out of scope

Not in item 3: MIDI, real note-on/off (item 4), glide/legato (item 4), note
priority (item 4), arpeggiator (item 5), accent (item 7), exponential envelope
curves (item 10/`character-and-vim.md` A2), a second envelope (explicitly forbidden
by CLAUDE.md).
