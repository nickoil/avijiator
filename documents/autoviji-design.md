# Autoviji design — random sequence fill

Design record for TODO item 8, "Autoviji — random sequence fill"
([TODO.md](TODO.md)). Written **after** implementation, not before — this
item was built inside a running session before the project's usual
"design doc first" discipline (CLAUDE.md, `documents/` step 0) caught up
with it. Recorded now for the same reason every other item gets one: so a
fresh session only needs "build Autoviji from documents/autoviji-design.md"
rather than re-deriving the decisions below from the diff.

## 1. What it is

A one-press "surprise me" button, labelled **AUTOVIJI**, sitting in the
header row immediately left of Audio Settings. One click:

1. Fills all 16 steps with a random note drawn from two fixed octaves.
2. Independently rolls each step's gate — a 1-in-8 chance of coming up off
   (silent), otherwise on.
3. Randomizes each step's per-step **Cutoff lane** (`stepCutoffNorm`, item
   7's existing per-step filter modulation — not the main VCF Cutoff knob).
4. Sets Autoviji's own default groove: Division `1/8T`, Pattern Length `8`.
5. Turns the sequencer on.

Every widget that reflects one of these atomics (the step grid, the On
toggle, the Division combo, the Pattern Length combo) is synced to match in
the same call, so the panel never shows stale state after a click.

## 2. Why these ranges

- **Two octaves, not one.** The first pass anchored on a single octave
  (C2..B2, matching `VoiceParameters::defaultStepMidiNote`) so a fresh
  pattern could never land in a surprising register. Widened to two octaves
  (C2..B3, MIDI 36-59) by ear during the session — one octave read as too
  narrow/samey for a "surprise me" feature. Still anchored at the same base
  (MIDI 36) rather than picking a fresh random anchor per click, so the
  register stays predictable even as the exact notes don't.
- **1-in-8 gate-off, not 1-in-12.** Also an ear-driven change from the
  original 1-in-12 - a denser pattern read as more "off" than intended at
  the wider odds.
- **Cutoff lane, not the main knob.** Resolved explicitly with the user
  before building: amber already means "arp playhead / step value" in the
  sequencer grid ([SynthPanel.cpp](SynthPanel.cpp)'s `StepCell::paint`,
  see `PanelLookAndFeel.h`'s `accentAlt` comment), and the per-step Cutoff
  lane is the sequencer's own existing per-step modulation concept - reusing
  it here doesn't introduce a second, competing way to modulate cutoff, it
  just randomizes a lane that was already there.
- **Division 1/8T + Pattern Length 8, not the panel defaults (1/16, 16).**
  Autoviji's own groove, not a copy of the sequencer's plain startup state -
  a shorter, swung pattern reads as a deliberate "here's a groove" rather
  than the same straight 16-step grid with different notes in it. Chosen by
  ear during the session, easy to change if a different default reads
  better.

## 3. Why writing the atomics directly is safe

`StepCell::mouseDrag`/`mouseUp` ([SynthPanel.cpp](SynthPanel.cpp)) already
write `stepPitchLog2Hz`/`stepGateOn`/`stepCutoffNorm` from the UI/message
thread today, via the plain relaxed-atomic helpers in
[ParameterControls.h](../Source/UI/ParameterControls.h) (`storeStepValue`,
`toggleStepFlag`). `randomizeSequence()` reuses those same helpers the same
way - no new threading pattern, no lock, and nothing on the audio callback
(this never touches `getNextAudioBlock`/`processBlock`). It shares the
sequencer's existing, already-accepted two-writer situation with live
pitch-record (`seqRecordArmed` - see `VoiceParameters.h`'s comment on
`stepPitchLog2Hz`) - not a new race, the same one item 7 already documented
and accepted: pressing Autoviji while Record is armed isn't a supported
combination, same as hand-editing a step while armed isn't.

Setting `seqDivision`/`seqPatternLength`/`seqEnabled` follows the same
"store the atomic, then sync the widget's own displayed state to match"
shape `attachToggle`'s own seed call uses in the constructor - the widgets'
`onChange`/`onClick` handlers are built to store *from* the widget, not the
other way round, so syncing them back after an external write means calling
`setSelectedId`/`setToggleState` with `juce::dontSendNotification` directly,
never routing back through `onChange`.

## 4. UI placement

**Not a SEQUENCER cell.** The first pass added Autoviji as a new
`PanelSection::addCell` inside the SEQUENCER section, after Lane - this
widened the section (a new `extraColumnWidth` and a bumped cell count in
`resized()`'s `place()` call). Moved to the header row, left of Audio
Settings, on request: the SEQUENCER panel's width goes back to exactly what
item 7 left it at, and Autoviji reads as a global "randomize + play"
action rather than one more sequencer control competing for space with
Division/Pattern Length/Tempo/Gate/Lane. Positioned via the same
`headerRow.removeFromRight(...)` pattern `audioSettingsButton` already uses,
just one call earlier so it lands immediately to that button's left.

**Amber button text**, via `TextButton::textColourOffId`/`textColourOnId`
set directly on the instance (not a `PanelLookAndFeel` override, which would
recolour every `TextButton` including Audio Settings) - matches the amber
knob pointer this button ultimately drives, so the button visually reads as
"the amber thing" the same way the knobs now do.

## 5. Out of scope, explicit

- **Accent, Slide, and the Resonance lane are left untouched.** Not part of
  the brief, and randomizing them wasn't asked for.
- **The main VCF Cutoff knob** stays wherever it was set - only the
  per-step lane is touched (section 2).
- **No new self-test.** This is UI-thread glue over `VoiceParameters` fields
  every existing step-sequencer self-test already exercises; there's no new
  DSP behaviour here to assert against.

## 6. Verification

Verified with a real Debug build + launch, clicking the button and
screenshotting the result: random notes land inside the intended range,
gate-off steps show up at roughly the expected rate, Division/Pattern
Length/On all update visibly to match, and unrelated knobs (Saw, Pulse,
Cutoff, Resonance, LFO...) are untouched.

**One methodology note, not a code bug**: several early test screenshots
during this session appeared to show unrelated knobs changing too. Traced to
leftover `Avijiator.exe` processes from earlier builds/launches in the same
session being captured instead of the freshly-built one (confirmed via
`tasklist`/`Get-Process` showing multiple instances) - not a real effect of
clicking Autoviji. Resolved once every stray process was killed before each
test; a single clean instance with the cursor kept off the button between
launches showed the intended behaviour and nothing else, consistently.

**Not yet done**: a full `cdb.exe` headless self-test pass since the
octave-range/gate-odds edit (per
[[cdb-headless-assertion-check]]) - the change is UI-thread glue with no new
self-test of its own (section 5), so low risk, but not re-confirmed this
session. No MIDI-hardware end-to-end test (none available this session, same
caveat every other item carries).

## Critical files

- [Source/UI/SynthPanel.h](../Source/UI/SynthPanel.h) - `autovijiButton`
  member, `randomizeSequence()` declaration.
- [Source/UI/SynthPanel.cpp](../Source/UI/SynthPanel.cpp) - button wiring
  and header-row placement (constructor, `resized()`), `randomizeSequence()`
  implementation.
- [Source/UI/ParameterControls.h](../Source/UI/ParameterControls.h) -
  `storeStepValue`/`toggleStepFlag`, reused unchanged.
- [Source/DSP/VoiceParameters.h](../Source/DSP/VoiceParameters.h) - the
  atomics this writes (`stepPitchLog2Hz`, `stepGateOn`, `stepCutoffNorm`,
  `seqDivision`, `seqPatternLength`, `seqEnabled`).
