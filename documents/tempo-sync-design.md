# Tempo sync design — shared master BPM, LFO ratio sync, wider LFO floor

Design record for the **Tempo sync** item ([TODO.md](TODO.md)'s parking-lot
entry, not yet numbered). Written before implementation, same discipline as
[arpeggiator-design.md](arpeggiator-design.md) (item 5) and
[step-sequencer-design.md](step-sequencer-design.md) (item 7).

`architecture.md`'s "Tempo sync" section and TODO.md's own entry have
described this feature since item 7 landed, with three explicitly open
questions. Settled this session (developer decision, not this doc's own call):

- **(a) One shared dial.** `arpTempoBpm` and `seqTempoBpm` are replaced by a
  single `masterTempoBpm` atomic. Arp and sequencer each keep their own
  `Division` combo, so rates stay proportional to the shared BPM rather than
  forced identical.
- **(c) Build now.** Item 7 (step sequencer) is done, so `StepClock.h:54-65`'s
  own condition for factoring anything out — "not before there are two real
  users" — is already satisfied.
- **(b) Still open, deliberately.** Whether glide time locks to tempo is
  explicitly **not** settled here — a different item, later.

Two things the developer asked for beyond the original TODO entry's wording:

- **LFO syncs to the master tempo, in multiples.** Satisfied by reusing
  `StepClock.h`'s existing `beatsPerStepForDivision`/`StepDivision` table —
  fractions of a beat (1/4 … 1/32, plus triplets), the exact shape
  `architecture.md:167-170` already anticipated ("a new Hz-from-BPM-and-
  division conversion, the same shape as StepClock's own table"). This is
  the *only* new table needed — no separate one for the LFO.
- **The LFO should be able to sweep even slower.** Deliberately **not**
  folded into the sync-ratio table above (no new slow multiples invented
  there) — handled as a wholly separate, free-running-mode-only change:
  widen `lfoRateHz`'s knob floor. The two asks sound related but resolve to
  independent code paths; conflating them would have coupled "slow" to
  "synced" when the developer only asked for the latter to support multiples, not
  for slow to require sync.

> **Model note**: none of this is item 2 or item 5's own oscillator/filter/
> clock *invention* — it's wiring together already-built, already-verified
> pieces (`StepClock`'s existing table, the existing `KnobSpec`/`ChoiceSpec`/
> `ToggleSpec` pattern). Not on CLAUDE.md's Opus list — every build step below
> is Sonnet, same reasoning `step-sequencer-design.md`'s own model note gives
> for item 7 despite it also touching `StepClock`/tempo.

---

## 1. Decisions and why

| Decision | Choice | Reasoning |
|---|---|---|
| Master tempo | One `masterTempoBpm` atomic (default 120.0f, 20-300 BPM — same range `StepClock::setTempo` already clamps to) replaces `arpTempoBpm`/`seqTempoBpm` | Developer's explicit "single tempo dial" choice |
| Clock ownership | Unchanged — `Arpeggiator` and `StepSequencer` keep fully independent `StepClock` instances and independent `Division` atomics | Only the BPM *source* is shared, not the clocks themselves; no test rig ever needs arp and seq at genuinely different tempos at once (checked: `SeqTransitionRig` already sets both to 300 BPM) |
| Tempo knob placement | **Reconsidered 2026-08-30** (post-build, developer call): moved to OUTPUT, next to Level. Originally landed in ARP (rename target only: `&arpTempoBpm` → `&masterTempoBpm`, label unchanged) — SEQUENCER's own Tempo knob was removed either way | ARP's placement was flagged at the time as "the simplest placement, not an independently-argued UX call" (zero layout-budget rework — SEQUENCER's row isn't width-budgeted, so losing a knob there doesn't touch row B's width). It read wrong once built: masterTempoBpm is a global control (arp, sequencer, synced LFO), and living in ARP implied arp-ownership. OUTPUT's plain global-controls cluster carries no such implication. Row A's `envRowWidthCompensation` was rebalanced (28px → 204px) in the same pass so row A/B stay pixel-matched |
| LFO sync ratio | New `lfoSyncEnabled` (int, default 0) + `lfoSyncDivision` (int, default `StepDivision::Sixteenth`) atomics, reusing `StepDivision`/`beatsPerStepForDivision` as-is | Matches arp/seq's own default convention; "off on first load" matches arpEnabled/seqEnabled/seqRecordArmed |
| LFO sync computation | Lives at the existing `lfo.setRate(...)` call site (`SynthVoice.cpp:187-191`), not inside `Lfo` itself | `beatsPerStepForDivision` is a free `constexpr` function (`StepClock.h:30-50`) — no new `StepClock` instance needed |
| LFO free-run floor | Widen `lfoKnobSpecs`' Rate row min from 0.02 Hz to ~0.005 Hz (~200s cycle), add `skewMidpoint = sqrt(min*max)` (≈0.316) | `Lfo::setRate` has no internal clamp — the knob range is the only enforcement today; skew matches Cutoff/Attack/Decay/Release's existing geometric-mean convention so the wider low end isn't crammed into the first few pixels of travel |
| Glide | Untouched | Open question (b), deliberately deferred |
| Slow (>1 beat) LFO sync ratios | Not built | The free-run floor covers "slower" instead — see intro |

---

## 2. Where `masterTempoBpm` lives

A new small labeled block in `VoiceParameters.h`, ahead of the item-5 (arp)
block, since it's no longer arp-owned — `masterTempoBpm` alone, with a
comment pointing at this doc and noting it replaces the two atomics
`Arpeggiator`/`StepSequencer` used to each own independently. The arp and
seq blocks stop declaring `arpTempoBpm`/`seqTempoBpm`; both simply read
`parameters.masterTempoBpm` where they used to read their own.

`Arpeggiator::process` (`Arpeggiator.cpp:170-179`) and
`StepSequencer::process` (`StepSequencer.cpp:53-55`) each already call
`clock.setTempo(bpm, division)` once per block, raw, unsmoothed — that call
shape is completely unchanged, only the atomic each reads its `bpm` argument
from moves. `StepDivision`/`arpDivision`/`seqDivision` are untouched.

---

## 3. LFO sync — the Hz-from-BPM-and-division conversion

`Lfo` gains no new API. `SynthVoice.cpp`'s existing block-rate read
(`SynthVoice.cpp:187-191`, right next to the `lfoWaveform` read) becomes:

```cpp
const auto effectiveHz = parameters.lfoSyncEnabled.load (std::memory_order_relaxed) != 0
    ? (float) (1.0 / ((60.0 / (double) parameters.masterTempoBpm.load (std::memory_order_relaxed))
                       * beatsPerStepForDivision ((StepDivision) parameters.lfoSyncDivision.load (std::memory_order_relaxed))))
    : parameters.lfoRateHz.load (std::memory_order_relaxed);

lfo.setRate (effectiveHz);
```

Read raw once per block in both branches, unsmoothed — same "time constant"
category `arpTempoBpm`'s own `VoiceParameters.h` comment already documents
(tempo/division/rate values are read raw, not smoothed, because they're
consumed at discrete instants, not continuously). `VoiceParameters.h` already
includes `StepClock.h` (for `arpDivision`/`seqDivision`), so `StepDivision`
and `beatsPerStepForDivision` are already in scope wherever `VoiceParameters`
is visible — no new include expected in `SynthVoice.cpp`, but confirm at
implementation time.

---

## 4. LFO's new Sync toggle — no stack needed

`SynthPanel.h`'s `ToggleStack` (`SynthPanel.h:88-109`) is hardcoded for
exactly 2 buttons (arp's On+Hold, seq's On+Record). LFO gains exactly 1 new
toggle (Sync), so it gets a plain `juce::ToggleButton` in its own single
cell via the existing `ToggleSpec`/`attachToggle` helper
(`ParameterControls.h:65-70,163-177`) — not a repurposed 2-slot stack, and
not a new stack variant. `numLfoToggles = 1` is new; LFO had no toggle
before this.

The Sync Division combo reuses the same `arpDivisionChoices[]` string table
already shared by arp and seq (`SynthPanel.cpp:26`, `static_assert` at
`SynthPanel.cpp:28-29`) — a third user of that one table, same "one table,
several `ChoiceSpec`s with different targets" precedent the arp/seq split
already established.

---

## 5. Row B width-budget growth

Row B (`LFO | KEYBOARD | ARP | OUTPUT`, `SynthPanel.cpp:985-994`) is
pixel-exact at 1240px today. LFO's cell count grows by 2 (new Sync toggle
cell + new Sync-Division choice cell: `numLfoChoices` 1→2, plus the new
toggle cell), and nothing in row B is being removed to compensate — ARP
keeps both its knobs (Tempo stays there), KEYBOARD and OUTPUT are untouched.

Precedent is growing the canvas, not cramming: item 7 grew `designHeight`
660→840 when SEQUENCER needed room ("pre-authorized by section 9" per that
comment in `SynthPanel.h`). This item does the same for width — grow
`designWidth` (and therefore row B's budget) by exactly the two new LFO
cells' worth of pixels. Exact pixel arithmetic is implementation-time work,
not fixed here; the principle — grow the canvas, don't rob KEYBOARD/ARP/
OUTPUT — is what this doc commits to.

SEQUENCER's own row (`SynthPanel.cpp:1001-1009`) is not width-budgeted
(left-aligned at natural width, per its own existing comment), so losing its
Tempo knob (`numSeqKnobs` 2→1) needs no compensation there at all.

---

## 6. Self-tests

- **Regression**: every existing arp/seq self-test keeps passing after the
  `arpTempoBpm`/`seqTempoBpm` → `masterTempoBpm` rename. Mechanical rename
  across `Arpeggiator.cpp:178,1109,1436` and
  `StepSequencer.cpp:54,390,600,617,674` (the fuzz test at
  `Arpeggiator.cpp:1436`, which randomizes tempo, is a pure rename — nothing
  about that test's logic depends on the atomic's old name).
- **New `runLfoTempoSyncSelfTest`** (name to be confirmed at build time):
  - Sync off leaves `Lfo` output byte-identical to today's free-run
    behavior — same "prove the inert path is truly inert" style as the step
    sequencer's own filter-lane tests (`StepSequencer.cpp`'s cutoff/resonance
    inert-then-live pair).
  - Sync on, at a couple of known BPM/division pairs, produces the
    arithmetically correct rate — proven via rendered-output period (cycle
    count over a known window), matching this codebase's "output is the only
    proof" convention throughout (every self-test drives real audio through
    real classes, never re-implements the loop it's checking) — not by
    reaching into `Lfo`'s private phase state.
- The widened Hz floor and the Tempo-knob relocation are knob-spec/layout
  changes only — nothing to `jassert` there. Flagged per CLAUDE.md's "What
  you cannot verify": whether the new floor actually *sounds* usefully
  slower, and whether the relocated Tempo knob reads sensibly in the panel,
  are both ears/eyes judgements only the developer can make.

---

## 7. Build order (all Sonnet — see the model note above)

1. `VoiceParameters` field changes (`masterTempoBpm`, `lfoSyncEnabled`,
   `lfoSyncDivision`) + read-site renames in `Arpeggiator.cpp`/
   `StepSequencer.cpp` + self-test rig renames. Build/cdb clean, no behavior
   change expected — pure regression at this step.
2. LFO sync computation in `SynthVoice.cpp` (section 3) + the new self-test
   (section 6).
3. Widened Hz floor + skew (section 1's table row) — independent of steps 1
   and 2, could land standalone if useful to verify by ear sooner.
4. UI: remove SEQUENCER's Tempo knob, add LFO's Sync toggle + Sync Division
   combo (sections 4-5), rebalance `designWidth`/row B, update the
   `static_assert` control-count totals (`SynthPanel.h:273-278`).
5. Full `cdb` self-test sweep (see `documents/CLAUDE.md`'s headless-
   verification precedent); tick TODO.md; move this item out of
   `architecture.md`'s still-open list, keeping (b) glide explicitly open
   there.

---

## 8. Out of scope, explicit

- **Glide-tempo sync** — open question (b), left for a future item.
- **Slow (>1 beat) LFO sync ratios** — the free-run floor (section 1) covers
  "slower" instead, per this session's decision. Nothing here prevents
  adding slow ratios to `lfoSyncDivision`'s table later if that turns out to
  still be wanted after living with the wider floor.
- **A shared clock/StepClock instance across arp and seq** — only the BPM
  *source* is shared; each still owns its own `StepClock` and its own
  `Division`. Merging the clocks themselves was never asked for and would
  couple two otherwise-independent, mutually-exclusive note sources for no
  benefit.

---

## Critical files

`Source/DSP/VoiceParameters.h`, `Source/DSP/StepClock.h` (read-only, reused
as-is), `Source/DSP/Lfo.h`/`.cpp`, `Source/DSP/SynthVoice.cpp`,
`Source/Arpeggiator.cpp`, `Source/StepSequencer.cpp`,
`Source/UI/SynthPanel.h`/`.cpp`, `Source/UI/ParameterControls.h`.
