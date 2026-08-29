# Step sequencer design — pattern, filter lanes, hand-over

Design record for **Stage A item 7** ([TODO.md](TODO.md)). Written before
implementation, same discipline as [dsp-voice-design.md](dsp-voice-design.md)
(item 2), [arpeggiator-design.md](arpeggiator-design.md) (item 5) and
[ui-design.md](ui-design.md) (item 6).

Items 2–6 are closed: the voice sounds right, plays from three input sources,
arpeggiates on a sample-accurate clock, and has a full control panel. Item 7
makes it play a *pattern* — a fixed 16-step sequence with per-step pitch,
gate, accent, slide, and (added to the original scope, see section 5) two
per-step filter lanes: cutoff and resonance.

Most of item 7 is not a fresh design problem. The arp (item 5) and the core
voice (item 2) already pre-declared the reuse points this item needs:

- **`Source/DSP/StepClock.h:54-65,159-163`** explicitly names item 7 as its
  second owner, and explicitly says *not* to abstract the sub-block render
  loop until there are two real users. So item 7 owns its own `StepClock`
  and writes its own loop, copying the arp's loop *shape*
  (`Source/Arpeggiator.cpp:165-331`), not a shared base class.
- **`arpeggiator-design.md` section 10** already names the mechanism for
  slide: the "Tie" idea — skip closing the gate across a step boundary so
  `SynthVoice` takes its legato branch and glide is audible. "The real slur
  is item 7's per-step slide flag."
- **`Source/DSP/SynthVoice.cpp:183-188`** already has a comment naming item
  7's accent as the second multiplier into the amplitude-modulation summing
  point. `SynthVoice.h:82-86` says the same for `currentVelocity`, captured
  on every `noteOn` today but never routed anywhere (confirmed by grep —
  zero other uses). Accent is realized as velocity, and building it also
  substantially implements `character-and-vim.md` B5's velocity routing as
  a side effect — intentional, not scope creep.
- **`Source/UI/SynthPanel.h:100-105`** already reserves an empty 90px "SEQ"
  strip; nothing built in it yet.
- **`Source/UI/ParameterControls.h`'s** `KnobSpec`/`ChoiceSpec`/`ToggleSpec`
  (lines 31-68) are all built around one pointer-to-member per control and
  cannot address "array + index." A 16-step × N-field pattern needs a new
  attach-helper shape, not a reuse of the existing one.

> **Model note**: item 7 is **not** on CLAUDE.md's Opus list (only items 2 and
> 5 are) — every build step below is Sonnet.

Cross-link: [step-automation.md](step-automation.md) covers *generalised*
per-step parameter automation (p-locks) — arbitrary parameters, sparse
storage, glide curves, an expression layer. That doc's data model is the
opposite of this one (sparse locks vs. this doc's dense, fixed-shape fields)
and is **not** imported here. This item's two filter lanes (section 5) are a
small, deliberate carve-out from that larger feature, not the start of
absorbing it. See section 13.

---

## 1. Decisions and why

| Decision | Choice | Reasoning |
|---|---|---|
| Clock | Own `StepClock` instance, own render loop | `StepClock.h:54-65` says not to share until there are two users |
| Pattern storage | Struct-of-arrays: parallel `std::array<std::atomic<T>, 16>` fields in `VoiceParameters` | Member-pointer spec structs can't index an array |
| Pattern length (v1) | Exactly 16 steps, single page | Paging to longer patterns deferred |
| Accent | Realized as velocity, new modulation routing in `SynthVoice` | `architecture.md:142`, `SynthVoice.cpp:183-188` |
| Slide | Reuse arp's "Tie" idea — skip force-close, glide via existing `Glide` | `arpeggiator-design.md` §10 |
| Filter automation (v1) | Two extra per-step fields, `stepCutoffNorm` + `stepResonanceNorm`, Hold-only (no cross-step glide) | See section 5 — added to original scope |
| Tempo | Own independent `seqTempoBpm` atomic, mirrors `arpTempoBpm` | Matches `StepClock` precedent; shared master tempo stays TODO.md's separate open item |
| Arp/Seq | Mutually exclusive **note** source | Only one side may own pitch/gate at a time — see section 6. Does **not** restrict filter modulation, which is not a note event |
| Pitch entry (v1) | Live step-record | Arm a toggle, play the existing MIDI/QWERTY/on-screen input, notes land into steps as the clock advances |
| Hold | Inert while seq drives | Same precedent as Glide/Legato going inert under the arp |

---

## 2. Reusing `StepClock`

Own a second `StepClock` member; call `prepare`/`reset`/`setTempo`
identically to the arp. Pattern position is
`clock.getStepIndex() % patternLength`, per `StepClock.h:159-163`'s own doc
comment on how a second owner should read step position.

---

## 3. The render loop

Template: `Arpeggiator.cpp:165-331` — two-deadline take-the-min shape,
close-before-open on a tie, force-close before opening a new note, advance
the clock even on a rest to keep grid phase, termination proof via strictly
decreasing remaining samples per iteration.

New per-step decision the arp's loop doesn't have: at each step boundary,
read `stepGateOn[index]`. Off means a rest — still call `advanceStep()` to
keep phase, no note event. On means read `stepAccent`/`stepSlide` and either:

- **Normal step**: force-close any currently open gate, then `noteOn` at the
  step's pitch with `velocity = accented ? 1.0f : 0.75f` (starting default,
  taste — see section 12).
- **Slide step**: skip the force-close while still gated, then `noteOn` —
  `SynthVoice` takes its legato `else` branch and glides instead of
  re-triggering (the arp's "Tie" mechanism, reused verbatim).

Independent of gate state — rests included, since a rest can still sweep the
filter — also read `stepCutoffNorm[index]` and `stepResonanceNorm[index]` at
every step boundary. See section 5 for how each is routed; the two are not
symmetric on the DSP side.

---

## 4. Pattern storage

```cpp
static constexpr int seqMaxSteps = 16;

std::array<std::atomic<float>, seqMaxSteps> stepPitchLog2Hz     {};
std::array<std::atomic<int>,   seqMaxSteps> stepGateOn          {};
std::array<std::atomic<int>,   seqMaxSteps> stepAccent          {};
std::array<std::atomic<int>,   seqMaxSteps> stepSlide           {};
std::array<std::atomic<float>, seqMaxSteps> stepCutoffNorm      {};
std::array<std::atomic<float>, seqMaxSteps> stepResonanceNorm   {};

std::atomic<int>   seqEnabled       { 0 };
std::atomic<int>   seqDivision      { (int) StepDivision::Sixteenth };
std::atomic<int>   seqPatternLength { seqMaxSteps };
std::atomic<float> seqTempoBpm      { 120.0f };
std::atomic<float> seqGateLength    { 0.5f };
```

Read by index at each step boundary — one read per field per step. Torn
reads across fields are harmless since the whole step is consumed together
at one instant (same "raw atomics, no smoothing at the point of read"
convention as `arpTempoBpm`/`arpGateLength`, `VoiceParameters.h:149-168`).

Needs a new index-based attach-helper in `ParameterControls.h` alongside the
existing member-pointer-based `attachKnob`/`attachChoice`/`attachToggle`.

---

## 5. Filter automation — cutoff and resonance are not symmetric

Added to the original item 7 scope after a direct question about whether the
sequencer should modulate the filter, not just pitch. Two fields,
`stepCutoffNorm` and `stepResonanceNorm`, share identical storage shape and
Hold-only snap semantics (no cross-step glide — that's `step-automation.md`
territory). **They are not symmetric on the DSP side**, verified directly
against `SynthVoice.cpp`, not assumed:

- **Cutoff already has a modulation summing point.**
  `cutoffModulationOctaves` at `SynthVoice.cpp:214-218` is additive, in
  octaves, and already fed by the envelope and the LFO:
  ```cpp
  const auto cutoffModulationOctaves =
      (routeEnvToFilter ? envValue * envToCutoffDepthSmoothed.getNextValue() : 0.0f)
      + lfoValue * lfoToCutoffDepthSmoothed.getNextValue();
  ```
  `stepCutoffNorm` becomes a third additive term in this same sum. Genuine
  reuse — no new summing point needed.

- **Resonance has no modulation summing point today.**
  `SynthVoice.cpp:219` reads `resonanceSmoothed.getNextValue()` straight
  from the knob into `filter.processSample` — confirmed by grep, zero
  env/LFO routing exists for resonance anywhere in the codebase.
  `stepResonanceNorm` therefore requires **new plumbing**: a
  `resonanceModulation` term needs adding, mirroring cutoff's additive,
  smoothed shape. The step sequencer becomes the first-ever consumer of
  resonance modulation in this instrument.

  **Settled, build order row 5**: a **simple additive 0..1 offset, clamped
  post-sum**, not cutoff's octave-style shape. Chosen because it keeps 0
  exactly inert without redefining `stepResonanceNorm`'s own zero default,
  needs no new depth knob, and matches `Vcf::processSample`'s feedback
  solution, which assumes resonance stays in `[0,1]` — an octave-style sum
  would need its own clamp back into range anyway, so the simpler shape wins
  outright rather than by a coin flip. Not yet tuned by ear (CLAUDE.md's
  "what you cannot verify") — a placed-not-measured starting point, easy to
  retune now that row 6 gives it a knob to feel through.

Both values are pushed through smoothing before reaching the DSP, never a
raw atomic snap — CLAUDE.md's "smooth cutoff/resonance... changes, or you
get zipper noise" constraint applies here same as anywhere else, and applies
per-step here specifically because a snapped value at 16th-note rates would
be far more audible than an occasional knob turn.

Rejected alternative, considered and declined: decoupling the sequencer into
a pure modulation source that runs *concurrently* under the arp (arp plays
notes, seq only sweeps the filter). This would mean relaxing the arp/seq
mutual-exclusivity decision (section 1) and reworking section 6's hand-over
arbitration into something arp-and-seq-simultaneous rather than
arp-or-seq. Not worth it: cutoff and resonance are modulation, not note
events, so they were never actually blocked by the arp/seq exclusivity rule
in the first place — a single sequencer step already plays a note *and* can
carry two more values at the same instant with zero ownership conflict. The
decoupled design would be justified only if the goal were "arp drives notes
while the sequencer runs independently at its own tempo" — that is a
different, larger feature and isn't what was asked for.

---

## 6. Architecture and hand-over — mutually exclusive note drivers

Extend `renderVoiceBlock` (`Arpeggiator.h:304-305`, `Arpeggiator.cpp:555-614`)
from a two-way arbitration (`arpWasOn` bool) to a three-way one: raw keys /
arp / seq (e.g. a small `VoiceOwner` enum). On any change, call
`releaseVoice` on every side that could own it — router, arp, seq, all
idempotent, same invariant as today: whichever side stops driving leaves it
silent, whichever side takes over starts from silence — then dispatch to
whichever of `arp.process` / `seq.process` / `voice.renderNextBlock` is now
active.

`StepSequencer` gets its own `releaseVoice`/`retakeVoice` mirroring
`Arpeggiator`'s. `NoteRouter::VoiceDrive` (`NoteRouter.h:56`) needs no
change — both arp and seq are `TrackOnly` consumers from the router's view.

This arbitration governs pitch/gate ownership only. Cutoff and resonance
modulation from the sequencer (section 5) apply regardless of which side
currently owns the voice's note — a filter sweep programmed into the
sequencer's pattern is a fair thing to want audible even while, say, the arp
is playing notes over it, and nothing in this design prevents that; it falls
out naturally from cutoff/resonance being read independently of gate state
in section 3. (Whether that combination is *exposed* in v1's UI — i.e.
whether the sequencer can run in a "filter-only" mode while the arp drives
notes — is left to section 9/11; the DSP has no objection to it, but it
wasn't asked for and isn't being built now.)

---

## 7. Transitions table (S1–S12, mirroring arpeggiator-design.md §7)

| # | Situation | Handled by |
|---|---|---|
| S1 | Seq ON while Arp already ON | Mutual exclusion: turning seq on releases the arp first |
| S2 | Arp ON while Seq already ON | Symmetric to S1 |
| S3 | Seq ON while a key is held/sounding | `router.releaseVoice`; `seq.releaseVoice` parks clock at step 0, first step fires at once (mirrors arp T1/T10) |
| S4 | Seq OFF mid-step, gate open (incl. mid-slide) | `seq.releaseVoice` force-closes the gate; `router.retakeVoice` re-asserts current key resolution (mirrors arp T2) |
| S5 | Current step is a rest while Seq runs | Gate-off deadline is independent of a note existing (mirrors arp T3) |
| S6 | Pattern length changed mid-run | Apply at next wrap (step 0), not mid-run — no in-flight rescale, same principle as tempo |
| S7 | A step edited live while its index is currently sounding | No retroactive effect; only affects the next visit to that index |
| S8 | `releaseResources()` / device stop mid-step | `seq.reset()` clears gate/clock/position; ordered with `voice.reset(); router.reset(); arp.reset(); seq.reset();` (mirrors arp T7) |
| S9 | Sample-rate change | `seq.prepare(sampleRate)` calls `reset()` (mirrors arp T8) |
| S10 | Env Destination = Filter, Seq running | Documented, not coded — accent's amp boost produces no articulation without VCA routing (mirrors arp T9) |
| S11 | Step has `slide = true`, `gate = false` | Slide is inert on a rest; previous note's gate-off deadline still fires normally |
| S12 | Hold engaged while Seq drives | Confirmed inert — same precedent as Glide/Legato going inert under the arp |

Carry forward the arp doc's honest limitation verbatim: pattern *steps* are
sample-accurate; *edits* to pattern data are consumed at whatever
granularity the audio thread reads the atomics, same class of caveat as
block-granularity key events. This applies to the two filter lanes too — a
live edit to a step's cutoff value lands whenever the audio thread next
reads it, not necessarily precisely at the step boundary.

---

## 8. Pitch entry — live step-record

**Built, build order row 7.** Record-arm toggle (`VoiceParameters::seqRecordArmed`,
a UI `ToggleStack` cell next to Seq On — `SynthPanel.cpp`); while armed **and**
the sequencer owns the voice, `StepSequencer::process` reads the router's
current priority-resolved pick — `NoteStack::getCurrentResolution`, the same
rule Keys itself sounds by, sampled once per block exactly like the arp's own
`liveNotes` — and writes it into that step's `stepPitchLog2Hz`/`stepGateOn`
immediately before reading them back for playback, so a freshly recorded step
sounds its own new value at once rather than one lap late. This deliberately
reuses `NoteRouter`'s existing block-granular live-tracking (the same
`TrackOnly` dispatch that already lets the arp read a live held set while it
owns the voice) rather than adding a new capture path — no NoteEventFifo
changes needed.

The three points this section left open are now settled, each easy to revise:

- **No key pressed at a boundary**: records a rest (`gateOn = false`),
  overwriting whatever the step held before — the plan this section already
  named.
- **Gate-length/tie behaviour while recording**: **not inferred.** A note held
  across several step boundaries records as a fresh gated (retriggering) step
  at each one, because recording only ever writes `stepPitchLog2Hz`/`stepGateOn`
  — `stepSlide`/`stepAccent` are untouched, so a tie across recorded steps is
  still a hand-edit in the grid afterward, exactly like accent already is.
  Simpler than inferring a tie from held-duration, and consistent with
  "filter lanes aren't touched by step-record" already deciding the same way
  for a different pair of fields.
- **Auto-stop vs. wrap**: **wraps.** Nothing clears `seqRecordArmed`
  automatically, so recording re-captures over the pattern for as long as it
  stays armed, standard loop-record convention for this genre of instrument.
  The alternative (stop after one pass) would need new state — an armed-since
  step index and an audio-thread write-back to disarm — for a less
  discoverable result (record silently turning itself off); wrap needs none
  of that.

Filter lanes are not touched by step-record — they're edited directly
(section 9), not captured from played input. `runStepRecordSelfTest`
(`StepSequencer.h`/`.cpp`) covers: disarmed recording is a no-op even with a
sounding resolution in hand (byte-identical pattern storage); armed recording
overwrites the current step's pitch and gate; armed recording with nothing
held writes a rest over a previously-gated step; accent/slide survive being
recorded over; and recording keeps re-capturing after a full (short, for a
fast test) pattern wraps, with the armed flag still on afterward.

---

## 9. UI

**Built, build order row 6** (Record's own toggle cell followed in row 7,
section 8). Replaced `SynthPanel::SeqReservedStrip` (was
`SynthPanel.h:100-105`, `SynthPanel.cpp:205-216,668`, `seqStripHeight = 90` at
`SynthPanel.cpp:587`) with a real `PanelSection`. The flagged resize
happened: `designHeight` grew 660 → 840 (`SynthPanel.h`) — pre-authorized
here, not a Polish-step afterthought, though it pushes the panel's aspect
ratio further from the Pixel's landscape shape than section 3 already
worried about.

New `StepCell` component (following `SynthPanel::PianoKey`'s precedent of a
hand-written `paint()`, `SynthPanel.cpp:219+`) for the visual states a step
needs: empty, gate-on, accent, slide, currently-playing, plus a
continuous-value readout for whichever of cutoff/resonance is the active
edit lane. `LookAndFeel`'s flat colour-swap model covers the discrete states
alone but not a continuous value — a bar-height or fill-amount encoding is
the natural fit, matching the "overlay the real value" spirit of
`step-automation.md` section 3a without importing that doc's machinery.

Two independent continuous lanes (cutoff, resonance) on the same 16 cells is
more than one — showing both at once per cell is likely to be cramped or
ambiguous, so v1 needs a lane-select (which value a drag on a step currently
edits), not a plan to render both simultaneously. Reuses
`PanelLookAndFeel::accentAlt` (`PanelLookAndFeel.h:35`, already earmarked for
"active state" colouring) for the currently-playing step highlight,
driven by `VoiceParameters::currentStepForUi`.

**Gesture, settled at build time** (`StepCell::mouseDown/mouseDrag/mouseUp`,
`SynthPanel.cpp`): plain click toggles Gate, right-click toggles Accent,
shift+click toggles Slide — all three lane-independent, so they work no
matter which lane is selected — and a vertical drag past a small pixel
threshold adjusts whichever lane the hand-wired Lane combo box (Pitch /
Cutoff / Resonance, UI-only, no `VoiceParameters` target of its own) currently
has selected: quantised semitones for Pitch, a proportional 0..1 change for
Cutoff/Resonance. A drag and a click are mutually exclusive per press
(`isDraggedFar` gates `mouseUp`'s click handling), not two gestures that could
both fire. **Not yet verified by hand** — synthetic input proved unreliable
for testing this in-session (documents/TODO.md's build step 6 entry has the
full reason) — so this gesture set is confirmed *implemented*, not yet
confirmed to *feel right*; the design was always "informed by how it actually
feels to use," and that check is still outstanding.

---

## 10. Self-tests

`runStepSequencerPatternSelfTest` (silent — storage read/write by index,
`stepIndex % patternLength` wrap, rest handling, filter-lane read-back) and a
transitions test (`runSeqTransitionSelfTest`, or an extension of
`runArpTransitionSelfTest` since S1/S2 is exactly the arp↔seq seam) driving
real blocks through the extended `renderVoiceBlock`, asserting actual
rendered silence at the right moments. Both wired into
`MainComponent.cpp:83-107`'s existing sequential Debug block. Same caveat as
every prior item's self-tests: `jassert` needs a debugger (F5) to actually
halt on failure — a plain launch staying up is not evidence these passed.

---

## 11. Build order (all Sonnet — item 7 is not on CLAUDE.md's Opus list)

| # | Name | Work | Ends with |
|---|---|---|---|
| 0 | Design | This document. No code | Design captured before implementation |
| 1 | Clock + pattern storage | `StepSequencer` class + own `StepClock`; new `VoiceParameters` arrays/atomics (pitch/gate/accent/slide + both filter lanes); `runStepSequencerPatternSelfTest` | Storage provably correct, silent |
| 2 | Render loop | Own sub-block loop; rest/gate handling; slide via Tie; accent hardcoded to a fixed elevated velocity (no depth routing yet); filter lanes read but not yet wired to DSP | **First audible milestone** — fixed test pattern plays |
| 3 | Accent DSP | New depth knobs; wire `currentVelocity` into amp and cutoff summing points in `SynthVoice.cpp` | Accent audibly punches harder |
| 4 | Hand-over / transitions | 3-way `renderVoiceBlock` arbitration; `StepSequencer::releaseVoice`/`retakeVoice`; walk S1-S12; `runSeqTransitionSelfTest` | Arp/Seq/keys hand over cleanly in any order |
| 5 | Filter automation lanes | `stepCutoffNorm` feeds the existing `cutoffModulationOctaves` sum; `stepResonanceNorm` gets a brand-new smoothed resonance summing point (resolve section 5's open question on its shape); both smoothed, no raw snaps | Per-step cutoff and resonance sweeps audible, no zipper noise |
| 6 | UI wiring | Index-based attach helper; `StepCell`/grid incl. cutoff/resonance lane-select gesture; On/Division/Tempo/Gate/PatternLength controls; replace reserved strip; resize if needed | Pattern (notes + cutoff + resonance) editable and playable from the UI |
| 7 | Pitch entry | Record-arm toggle; capture live note events into pattern storage | Pitches entered by playing |
| 8 | Polish | Comment pass; reconcile doc; tick TODO item 7 | Item 7 closed |

---

## 12. Verification (shaped like arpeggiator-design.md §12)

- **Objectively claimable**: builds clean Debug+Release; self-tests pass
  under a debugger (same F5-only caveat carried forward, unresolved); with
  `seqEnabled = 0` and new velocity-depth/resonance-modulation knobs at
  default, audio path is byte-identical to item 6.
- **Needs a human at the speakers**: accent audibly punchier; slide glides
  without re-triggering the envelope; tempo/division matches a metronome
  independently of the arp's own tempo; record-arm actually captures what
  was played; every arp↔seq↔keys toggle order ends in silence with all keys
  up; per-step cutoff and resonance sweeps are audible and click-free at
  16th-note rates.
- **Taste**: accent depth defaults; default step velocity (0.75 starting
  point); whether live-record is the right pitch-entry UX; whether
  resonance's modulation shape (section 5's open question) sounds right;
  whether it feels like an acid line.
- **Unverifiable — flag, don't claim**: MIDI hardware end-to-end (same gap
  items 4/5 carry); Pixel touch/CPU cost; sub-sample step accuracy by ear.

---

## 13. Out of scope (carried forward explicitly)

**Generalised** per-step parameter automation / p-locks
(`step-automation.md`) — arbitrary parameters beyond cutoff/resonance, glide
curves/tension, expressions, sparse-lock storage, sub-step resolution. This
doc's two Hold-only filter lanes are a deliberate, small carve-out from that
larger feature, not the start of importing it wholesale. Also out of scope:
paging beyond 16 steps; a shared master tempo (TODO.md's separate "Tempo
sync" item); swing/humanisation (`character-and-vim.md` B2 — though
`getStepIndex()` already gives it a parity source for later); polyphony;
host sync; running the arp and sequencer as simultaneous, independent note
sources (section 5's rejected alternative).

---

## Critical files

- `Source/DSP/StepClock.h` — reused as-is, second owner
- `Source/Arpeggiator.h` / `Source/Arpeggiator.cpp` — render-loop and
  hand-over template (`renderVoiceBlock`, `process`, `gateSamplesForStep`)
- `Source/DSP/SynthVoice.h` / `Source/DSP/SynthVoice.cpp` — `noteOn`/
  `noteOff`, amplitude and cutoff modulation summing points. `stepCutoffNorm`
  reuses the existing cutoff sum (`SynthVoice.cpp:214-218`);
  `stepResonanceNorm` requires a new resonance modulation summing point
  (`SynthVoice.cpp:219` currently has no modulation input at all)
- `Source/DSP/VoiceParameters.h` — new atomics/arrays, following the arp's
  section 149-172 shape
- `Source/NoteRouter.h` — `VoiceDrive`, `releaseVoice`/`retakeVoice`
- `Source/UI/ParameterControls.h` — new index-based attach helper
- `Source/UI/SynthPanel.h` / `Source/UI/SynthPanel.cpp` — `SeqReservedStrip`
  replacement
- `documents/TODO.md`, `documents/architecture.md` — item 7's settled spec,
  Tempo sync section
