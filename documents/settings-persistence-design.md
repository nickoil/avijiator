# Settings persistence design — session-state auto-restore + named presets

Design record for TODO item 9, "Settings Persistence"
([TODO.md](TODO.md) lines 182-206). Written before implementation, same
discipline as [arpeggiator-design.md](arpeggiator-design.md) (item 5),
[step-sequencer-design.md](step-sequencer-design.md) (item 7), and
[tempo-sync-design.md](tempo-sync-design.md) (unbuilt).

Nothing is persisted today except audio/MIDI device selection — a separate,
narrower feature (TODO.md's "Remember audio/MIDI device settings across
restarts", already built) that this item reuses the idiom of but does not
touch the code of. `juce_data_structures` is already linked
(`CMakeLists.txt:99`) for that feature, so no build-system change is needed
here.

TODO.md's own writeup left three things open: (a) file format, (b) where
shipped presets live, (c) build-order interaction with tempo-sync. Settled
this session (developer decisions, not this doc's own call), plus two more that
came up during design:

- **(a) Format: `juce::XmlElement`.** Matches the only existing serialization
  precedent in this codebase (`MainComponent`'s device-settings code, see
  section 3). `juce::ValueTree` has zero uses anywhere in `Source/` — not
  introduced here for no benefit.
- **(b) Factory presets live in the user's settings folder, written at first
  launch — not embedded `BinaryData`.** Editable in place, no rebuild to tweak
  a preset; revisit embedding only if this app is ever actually distributed
  (`architecture.md`'s licensing section is explicit that nothing there
  applies yet).
- **(c) Order-independent by construction.** The serializer enumerates
  parameters by name via the existing `KnobSpec`/`ChoiceSpec`/`ToggleSpec`
  member-pointers (section 4), so whichever of this item or tempo-sync lands
  first, the other doesn't require serializer surgery — just the ordinary
  field-rename consequence documented in section 2.
- **Latch scope (new question, not in TODO.md's original three): the arp's
  latched Hold chord is part of the same serialized data for both tiers** —
  no special-casing between session-state and named presets.
- **Preset browser UI is designed and built in this item**, not deferred to
  a later pass the way item 6 got its own standalone UI item.

> **Model note**: like tempo-sync-design.md, none of this is item 2 or 5's
> own DSP invention — it's serialization plumbing over already-built,
> already-verified parameter storage. Not on CLAUDE.md's Opus list; every
> build step below is Sonnet.

---

## 1. Decisions and why

| Decision | Choice | Reasoning |
|---|---|---|
| Serialization format | `juce::XmlElement` | Matches `MainComponent`'s existing `ApplicationProperties`/`createStateXml()` idiom; no second serialization pattern introduced |
| Two-tier model | Tier A (silent session-state auto-restore) + Tier B (named presets), sharing one serialization function | Tier A is just an anonymous preset — building two independent save/load paths would duplicate the one piece of code worth getting right |
| Factory presets | Developer folder, written at first launch | No rebuild to tweak a preset; embedding is a distribution-time concern this project hasn't reached (`architecture.md:22-25`) |
| Latch scope | Same serialized data for both tiers, no special-casing | Simpler mental model; a chord latched when the app last closed is exactly as "current state" as any knob position |
| Preset-UI scope | Designed and built in this item | Developer's explicit call — ships as one complete feature rather than a backend with no front door |
| Preset-UI shape | Two header-row buttons (Save, Load), each opening its own dialog; nothing added to the fixed canvas | Developer's explicit call, settled after a dropdown-at-top-of-canvas framing was floated and dropped — see section 6 |
| Duplicate preset names | Save is rejected inline, no overwrite, no auto-suffix | Developer's explicit call — forces an unambiguous name per preset rather than silent overwrite or piling up auto-numbered duplicates |
| Version/rename policy | Best-effort by name, no migration shims, no warning | See section 2 — accepted trade-off for a fast-evolving solo project, walked through concretely against the tempo-sync rename before being settled |
| Parameter enumeration | Generic, over `SynthPanel`'s existing spec tables via member-pointers | Survives future field renames (tempo-sync's `masterTempoBpm` merge) with zero serializer changes, instead of a hand-maintained field list that silently drifts |
| Arp-latch snapshot | Plain per-slot atomics on `Arpeggiator`, consumed at the top of the next `process()` block | Matches this codebase's existing "torn read is harmless" convention (step-sequencer arrays) and "check a flag at block boundary" convention (`VoiceOwner` hand-over) rather than inventing new machinery for a rare, one-shot event |
| Load concurrency | No `presetLoadInProgress` gate — a load is many independent atomic writes, may render a torn mix mid-load | Direct consequence of the torn-read convention already accepted for ordinary knob turns; a preset load isn't categorically different, just bigger |
| File placement | New `Source/Presets/` subfolder | Tier B's UI and the factory-preset bank belong in the same feature home as the serializer, rather than distorting `DSP/`'s voice-internal scope |

---

## 2. What a preset holds

Enumerated from `Source/DSP/VoiceParameters.h`:

- **~25 spec-table-covered scalars** — the 19 knobs + 6 choices + toggles
  already wired in `SynthPanel.cpp`'s 13 spec tables (`SynthPanel.h:289-304`).
- **3 non-spec-table scalars**, requiring hand-written serializer entries
  since no `SynthPanel` spec table targets them: `velocityToAmpDepth`
  (`VoiceParameters.h:156`), `velocityToCutoffDepthOctaves`
  (`VoiceParameters.h:167`), `seqPatternLength` (`VoiceParameters.h:312`,
  deliberately not a `ChoiceSpec` per `SynthPanel.h:250-256`'s
  index/value-offset comment).
- **6×16 step-sequencer arrays** — `stepPitchLog2Hz`, `stepGateOn`,
  `stepAccent`, `stepSlide`, `stepCutoffNorm`, `stepResonanceNorm`
  (`VoiceParameters.h:261,269,275,281,293,305`), read/written via the
  existing `loadStepValue`/`storeStepValue` index-based helpers
  (`Source/UI/ParameterControls.h:192-204`).
- **Arp latch snapshot** — `latched`/`numLatched`/`latchAwaitingFreshChord`
  (`Source/Arpeggiator.h:265,266,272`). New plumbing; see section 5.

**Explicitly excluded** — re-derived or genuinely transient, not parameters:
- `currentStepForUi` (`VoiceParameters.h:363`) — the one audio→UI playhead
  atomic, not a saved value.
- `StepSequencer`'s own runtime state — `clock`'s phase, `gateIsOpen`,
  `samplesUntilGateOff` (`StepSequencer.h:179,185-186`) — re-derived from the
  pattern and the clock division on the next play, not stored.
- Raw live-held-key state (the note-priority stack's currently-held notes) —
  transient input, not something a preset load should reach in and fabricate.

### Version/rename policy — best-effort, no migration, no warning

`formatVersion` is a root-element attribute from day one, but its only job is
to be present for a human diffing a saved file later — the loader does not
branch on it. The actual mismatch-handling rule: **the loader matches each
saved field by name against whatever fields the *current* build exposes.**
A saved field with no matching current field is silently ignored. A current
field with no matching entry in the file is silently left at its default. No
per-rename migration shim, no log line, no UI warning.

This was settled explicitly, after walking through what it means for the one
rename already on the horizon. When tempo-sync lands
(`arpTempoBpm`/`seqTempoBpm` → `masterTempoBpm`, per
[tempo-sync-design.md](tempo-sync-design.md) section 2): any preset or
session-state file saved *before* that rename loses its saved tempo the first
time it's loaded *after* — it silently resets to `masterTempoBpm`'s default
(120 BPM) rather than carrying over either old value. This is doubly true
because arp and seq could have held genuinely different tempos before the
merge, so even a hypothetical migration shim would need an arbitrary tiebreak
— there's no principled automatic answer, only an arbitrary one. Accepting
the loss outright, with nothing pretending otherwise, was judged better than
a tiebreak rule nobody would remember the reasoning for. If this proves
annoying in practice once lived with, add a warning (option considered and
deferred, not rejected) rather than a migration shim.

---

## 3. The two tiers, concretely

**Tier A — session-state auto-restore.** Mirrors `MainComponent`'s existing
device-settings code exactly (`MainComponent.cpp:11-21,197-225,299-301`):
same `juce::ApplicationProperties appProperties` instance
(`MainComponent.h:100`), a second key alongside `audioDeviceStateKey`
(`MainComponent.cpp:21`) — e.g. `constexpr const char* synthStateKey =
"synthState";`. Loaded once in the constructor (after device state, before
the app is interactive), saved in the destructor alongside
`deviceManager.createStateXml()`, via `appProperties.saveIfNeeded()`. No new
UI.

**Tier B — named presets.** User-facing save-as/load/browse/delete, backed by
one file per preset (not one big blob) in a new `Presets/` folder alongside
the existing `ApplicationProperties` settings folder — independently
shareable/deletable, and consistent with "user-folder, editable in place"
from section 1. UI shape: see section 6.

Both tiers call the **same** `toXml(const VoiceParameters&, const
Arpeggiator&)` / `fromXml(const juce::XmlElement&, VoiceParameters&,
Arpeggiator&)` pair (section 4) — Tier A is simply an anonymous preset saved
under a fixed key instead of a user-chosen filename.

---

## 4. Generic enumeration over spec tables

`SynthPanel`'s 13 `KnobSpec`/`ChoiceSpec`/`ToggleSpec` static tables
(`SynthPanel.cpp:109-265`, declared `SynthPanel.h:289-304`) are the existing
source of truth for "what's a parameter" — each entry already carries a
`std::atomic<float> VoiceParameters::*` or `std::atomic<int>
VoiceParameters::*` member-pointer (`ParameterControls.h:33-70`). The
serializer reuses this directly rather than maintaining a second,
independently-drifting table: a new accessor on `SynthPanel` (a `friend`
declaration for the serializer, or a small static method returning the flat
list of entries) hands out `{key, member-pointer}` pairs for all ~25
spec-covered fields. A future rename that edits these tables (tempo-sync's
`masterTempoBpm` merge will have to, regardless of build order) keeps the
serializer in sync automatically — no second edit site.

**Correctness requirement — the lookup key cannot be the UI display label.**
`KnobSpec::name` is a *display* string, and it collides today: `arpKnobSpecs`
and `seqKnobSpecs` each have an entry labeled "Tempo"
(`SynthPanel.cpp:199-231,241-259`), pointing at two different
`VoiceParameters` fields (`arpTempoBpm`/`seqTempoBpm`). Serializing by
display name would silently merge the two into one XML attribute, so the
spec structs gain a second string distinct from the display label — e.g.
`serializedName` (`"arp.tempo"` / `"seq.tempo"`) — used only by the
serializer, never shown in the UI. Every spec-table entry needs one at
implementation time; this is mechanical but must not be skipped for even one
entry, since a missing key silently drops that field from every save.

Hand-written entries (not spec-table-derived) cover the 3 non-spec scalars
and the 6×16 step arrays from section 2, using the same `serializedName`
convention for consistency, and reusing `loadStepValue`/`storeStepValue`
(`ParameterControls.h:192-204`) for the array indices.

**Built, two deviations from the above, both found empirically rather than
foreseen here:**

1. **Table-name prefix instead of a `serializedName` field on every entry.**
   `SynthPanel::forEachSerializableParameter` (declared `SynthPanel.h`,
   defined `SynthPanel.cpp`) walks all 16 (not 13 - LFO's Sync toggle and
   OUTPUT's own section postdate this doc) `KnobSpec`/`ChoiceSpec`/
   `ToggleSpec` tables and keys each entry `"<table>.<display name>"` (e.g.
   `"arp.Gate"`, `"seq.Gate"`) instead of adding a hand-typed
   `serializedName` string to every one of the ~33 entries. Same uniqueness
   guarantee this section asked for - a collision needs the SAME table AND
   the same display name, and no table repeats a display name internally -
   with no new field to type, and risk skipping on one entry, across every
   existing initializer. (The exact "Tempo"/"Tempo" collision this section
   used as its motivating example no longer exists - `tempo-sync-design.md`
   merged `arpTempoBpm`/`seqTempoBpm` into one shared `masterTempoBpm` before
   this item was built - but the underlying `KnobSpec`/`ChoiceSpec` correctness
   requirement is still real: "Gate" (`arpKnobSpecs`/`seqKnobSpecs`),
   "Division" (`arpChoiceSpecs`/`seqChoiceSpecs`), and "On"
   (`arpToggleSpecs`/`seqToggleSpecs`) all still collide on display name
   today, and the self-test's Scenario 1 proves the prefix keeps them
   distinct by giving every visited field its own value via a running
   counter, not a hand-picked pair.)
2. **`<Param key="..." value="..."/>` child elements, not plain XML
   attributes keyed by name.** Several display names contain characters an
   XML attribute NAME cannot - a space (`"Pulse Width"`, `"Glide Time"`,
   `"Sync Division"`, `"Note Priority"`) or `->` (`"Env->Cutoff"`,
   `"->Pitch"`, `"->Cutoff"`). `xml->setAttribute(key, value)` with such a
   key fired a real `JUCE_ASSERT` (`xml/juce_XmlElement.cpp:76`) the first
   time this ran under the project's cdb self-test check - not a case
   reasoned about in advance. An attribute VALUE has no such restriction, so
   each entry became its own `<Param>` child instead, with the key carried
   as `key="..."` data rather than as the element's own attribute name.
   `PresetSerialization.cpp`'s `findParam()` does the linear key lookup on
   load - ~33 entries, a rare message-thread operation, so an O(n²) scan
   costs nothing worth optimizing.

---

## 5. Arp-latch thread safety (new plumbing — doesn't exist today)

`Arpeggiator::latched`/`numLatched`/`latchAwaitingFreshChord`
(`Arpeggiator.h:265,266,272`) are plain, non-atomic, audio-thread-private
members today, with zero public accessors. `resolveActiveNotes`
(`Arpeggiator.h:223`) is the only existing code that touches them, and it's a
live-input mutator, not a snapshot/inject API. Saving or loading a preset
happens on the message thread, so both directions need new, genuinely
lock-free plumbing.

**Save direction (audio → message thread).** The latch changes rarely — on a
hold-mode chord change, not every block — and a save only needs the *latest*
state, not a queue of every change. This rules out a FIFO
(`NoteEventFifo`-style) and points at the same shape `currentStepForUi`
already establishes for one-directional audio→UI handoff
(`VoiceParameters.h:352-363`): plain individual atomics per latch slot,
living on `Arpeggiator` itself — a `std::array<std::atomic<int>,
maxHeldNotes>` for note numbers plus a parallel `std::atomic<float>` array
for velocity/pitch, updated by the audio thread whenever the latch actually
changes, read by the message thread only at the moment of a save. This
matches the existing "torn read across fields is harmless" convention
already documented for the step-sequencer arrays (`VoiceParameters.h:236-238`)
— a save reading a half-updated latch mid-change is no worse than a save
catching two knobs mid-turn, and no heavier double-buffer/generation-counter
machinery (which `StepGrid`'s live 30Hz polling would justify) is needed for
a rare, developer-initiated, one-shot read.

**Load direction (message thread → audio).** A new public method, e.g.
`Arpeggiator::requestLatchLoad(const LatchSnapshot&)`, called from the
message thread: stores the incoming snapshot into a pending-load buffer and
sets `std::atomic<bool> hasPendingLatchLoad`. `Arpeggiator::process()` checks
that flag once at the top of every block — entirely on the audio thread,
matching the "check a flag at block boundary" shape `VoiceOwner` hand-over
already uses — and if set, copies the pending snapshot into
`latched`/`numLatched`/`latchAwaitingFreshChord` and clears the flag. This
preserves today's invariant that those three fields are only ever touched
from the audio thread; the message thread only ever writes the separate
pending-snapshot atomics.

**Ordering requirement.** A preset load also loads `arpEnabled`/`arpHold`
(ordinary scalar fields, via section 4's mechanism) in the same operation.
The loader must set `hasPendingLatchLoad` no later than it writes those
scalar atomics, so both land together by the next block boundary — stated
explicitly here so the load routine's write order isn't left to chance at
implementation time.

**No interaction with `VoiceOwner` arbitration.** Priming the latch is
independent of whether the arp currently owns the voice
(`Arpeggiator.h:319-320`) — `resolveActiveNotes`/`chooseNextIndex`
(`Arpeggiator.h:170,223`) already treat `latched` as inert unless
`arpHold`/`arpEnabled` are also on. Injecting a latch while the arp is off,
or while Seq/Keys currently own the voice, simply sits primed until the arp
is switched on — exactly the "ships already arpeggiating a chord" scenario
TODO.md describes. No new arbitration logic needed.

**Built, one simplification from "updated... whenever the latch actually
changes" above:** `publishLatchSnapshot()` is called unconditionally at
`resolveActiveNotes()`'s single exit point (restructured from its original
four independent `return`s to get one call site) every time it runs, not
only when the latch's contents actually differ from last time. Cheap - a
bounded, `NoteStack::maxHeldNotes`-sized array of atomic stores - and this
is also what makes the self-test's own approach valid: the test drives
`resolveActiveNotes()` directly, exactly as section 10 asks for, and a
"changed" latch reached via `Arpeggiator`'s real public API is exactly the
"whenever it changes" event that section already described - unconditional
publication just avoids adding a second, redundant comparison to detect
what the caller already knows it just did.

---

## 6. Preset browser UI

Two buttons, **Save** and **Load**, added to `SynthPanel`'s existing header
row immediately to the left of `audioSettingsButton`
(`SynthPanel.h:471`, `SynthPanel.cpp:861-868,928-933`) — same row, same
right-aligned header strip the Audio Settings button already occupies, so
**nothing grows on the canvas** (developer's explicit call — this supersedes an
earlier "dropdown at the top of the page" framing floated mid-session, which
would have needed canvas room and was dropped). Each button opens its own
`juce::DialogWindow`, reusing the same mechanism `MainComponent`'s existing
`showAudioSettings()` already establishes (`MainComponent.cpp:238-261`),
rather than one combined modal:

- **Save** → a small dialog with a single name text field and a Save/Cancel
  action. **Names must be unique**: if the entered name matches an existing
  preset file, the dialog rejects inline (e.g. "A preset named 'X' already
  exists — choose a different name") and stays open — no silent overwrite,
  no auto-suffixing (developer's explicit call).
- **Load** → a dialog with a scrollable list of existing preset names, one
  row each, with a Load action and a Delete action per row. Delete lives
  here, not on the Save dialog or anywhere in the main canvas.

This is the one place in this doc where pixel-level layout is deliberately
left to the build step itself, consistent with how `architecture.md`'s UI
section treats "real answer needs a human at the screen" — the two dialogs'
starting shape (name field; scrollable list with load/delete per row) is a
reasonable first pass, not a locked mockup.

**Built:** the pixel-level placement this section left open settled as
exactly between `autovijiButton` and `audioSettingsButton` (developer's explicit
call) — left-to-right, Autoviji, Save, Load, Audio Settings. Both dialogs'
content and the factory-bank writer live in `Source/Presets/
PresetBrowserUI.h/.cpp`, a free-function namespace rather than a class -
neither dialog has state that outlives the single call that launches it.
Both close via `findParentComponentOfClass<juce::DialogWindow>()->
exitModalState(0)` from their own Save/Cancel/Load button `onClick`s - the
same self-deleting-on-close mechanism `showAudioSettings()`'s own comment
already documents (`LaunchOptions::launchAsync()`'s modal state is entered
with `deleteWhenDismissed = true`), just triggered programmatically instead
of only via the native title bar's close box.

**Built, a gap this section didn't anticipate: the panel's WIDGETS need
pushing back into sync after a load, separately from the atomics.**
`fromXml` only ever writes into `VoiceParameters`/`Arpeggiator` atomics -
exactly right for the audio thread, which reads those directly and would
have started sequencing/arpeggiating correctly either way - but every
slider, combo box and toggle on `SynthPanel` was attached to its atomic
once, at construction, via `attachKnob`/`attachChoice`/`attachToggle`'s own
one-time seed call. Nothing was pulling a widget's displayed position back
from the atomic on any LATER external change, because until this item the
only such change was `masterOctaveShift` via comma/period (`refreshOctaveReadout`,
documents/note-handling-design.md section 7). A developer bug report (Load not
lighting the SEQUENCER On toggle, despite the sequencer correctly being
about to run) surfaced that a preset load is a second, much larger case of
exactly that same gap - every field, not one.

Fixed generically rather than one toggle at a time: `ParameterControls.h`
gained `refreshKnob`/`refreshChoice`/`refreshToggle` - the inverse of
`attachKnob`/`attachChoice`/`attachToggle`'s own seed call, pushing the
atomic's current value into the widget with `dontSendNotification` instead
of the other direction - and `SynthPanel::refreshControlsFromParameters()`
walks the same 16 tables `forEachSerializableParameter`/the constructor's
`wireKnobs`/`wireChoices` already walk, calling the matching `refresh*` on
each cell. `MainComponent` calls it after Tier A's startup restore AND
after a Tier B Load completes (`PresetBrowserUI::showLoadDialog` grew an
`onLoaded` callback parameter for the latter, since the load itself happens
asynchronously, inside the dialog, well after `showLoadDialog` itself
returns). `stepGrid.repaint()` covers the step arrays, which paint straight
from `VoiceParameters` every time already - no separate refresh path
needed there.

---

## 7. Concurrency during a full preset load

A load writes ~25+ scalars, 96 step-array entries, and the latch snapshot —
not atomic as a whole operation. A block rendered mid-load can read a torn
mix of old-preset and new-preset values. This is accepted as a direct
consequence of the same "torn reads are harmless" convention already applied
to the step arrays and to ordinary simultaneous knob turns — a preset load
isn't categorically different in kind, only larger in the number of fields
touched at once. No `presetLoadInProgress` gate is added. If a specific
audible glitch on load ever turns up in listening, that's the trigger to
revisit this, not a hypothetical one.

---

## 8. File placement

New `Source/Presets/` subfolder — `PresetSerialization.h/.cpp` for the
`toXml`/`fromXml` pair and section 4's spec-table accessor, growing later to
hold the preset-browser dialog (section 6) and any factory-preset loading
code (section 9). Chosen over `Source/DSP/` (would pull `Arpeggiator`
plumbing into a folder documented as voice-internal,
`VoiceParameters.h:11-16`'s own admitted wrong-direction-include territory)
and over a flat `Source/PresetSerialization.*` (viable, but a dedicated
subfolder gives the feature one clear, growing home rather than scattering
serializer/UI/factory-bank code across the flat `Source/` root).

---

## 9. Factory preset bank

A curated starting set, written to the same user-folder `Presets/` location
(section 3) at first launch if that folder doesn't yet exist — not embedded
`BinaryData`, per section 1's settled decision. Content (which patches, how
many) is implementation-time work, not fixed here.

---

## 10. Self-tests

`runPresetRoundTripSelfTest`, `#if JUCE_DEBUG`-gated, appended to
`MainComponent.cpp`'s existing sequential self-test block after
`runStepRecordSelfTest()` (`MainComponent.cpp:159`) — same "drive the real
thing, prove via output/state, don't re-implement the logic being tested"
discipline as every other self-test in this codebase
(`arpeggiator-design.md` section 12, `step-sequencer-design.md` section 10).

- Construct a `VoiceParameters` with every field set to a known,
  **non-default** value — an all-defaults fixture would let a silent no-op
  pass. Deliberately include `arpTempoBpm` and `seqTempoBpm` set to two
  *different* values, so a display-name key collision (section 4's
  correctness requirement) fails this test as a wrong-value mismatch instead
  of passing unnoticed. Include at least one non-zero entry in each of the
  six step arrays.
- Round-trip through `toXml`/`fromXml` into a **fresh** `VoiceParameters`;
  assert exact field-for-field equality (`.load() == .load()` per atomic, no
  epsilon — an XML round-trip losing float precision is itself a bug worth
  catching, matching this project's existing exact-not-approximate self-test
  posture). Assert `currentStepForUi` is untouched (explicitly excluded from
  serialization).
- A second scenario for the arp latch: drive a real `Arpeggiator` into a
  held-chord state via its actual public API (`resolveActiveNotes`, not a
  hand-set private field), snapshot it (section 5), inject the snapshot into
  a **second, fresh** `Arpeggiator`, and assert the second instance's next
  `process()` call renders the same notes the first would have — proven via
  rendered output, matching `runArpTransitionSelfTest`'s own convention,
  never by reaching into `latched` directly.
- A version-mismatch case: feed `fromXml()` an XML blob missing a field that
  exists in the current build (simulating "loaded an old save after a
  rename") and assert it's silently left at its default, per section 2's
  settled policy — proving the *accepted* behavior, not treating it as a bug
  to guard against.

---

## 11. Build order

0. **This document.**
1. **Generic scalar save/load + Tier-A auto-restore.** Add
   `Source/Presets/PresetSerialization.h/.cpp` with `toXml`/`fromXml` driving
   the ~25 spec-table-covered scalars via section 4's mechanism (including
   the `serializedName` addition to `KnobSpec`/`ChoiceSpec`/`ToggleSpec` and
   the `SynthPanel` accessor). Wire Tier A into `MainComponent`'s existing
   `appProperties` (section 3) — a new `synthStateKey`, loaded in the
   constructor, saved in the destructor. **Done when**: changing a knob,
   relaunching, and seeing it restored works by hand. Build/cdb clean, no
   behavior change to anything else.
2. **Non-spec scalars + step-sequencer arrays.** Hand-written entries for
   `velocityToAmpDepth`/`velocityToCutoffDepthOctaves`/`seqPatternLength`;
   loop the 6×16 step arrays via `loadStepValue`/`storeStepValue`. **Done
   when**: a full `VoiceParameters` (minus the arp latch) round-trips.
3. **Arp-latch plumbing.** Section 5's save-side atomics and load-side
   `requestLatchLoad`/pending-flag mechanism on `Arpeggiator`, wired into the
   same serializer. Isolated as its own step since it's the one genuinely
   novel piece of lock-free plumbing in this item — a regression here should
   never be confused with a problem in the much more mechanical scalar/array
   code from steps 1-2. **Done when**: a preset holding a latched chord
   restores it, provable via the self-test's second scenario.
4. **Preset browser UI.** Section 6's dialog — save-as/load/list/delete.
   **Done when**: Tier B is usable end-to-end from the panel.
5. **Factory preset bank.** Section 9 — a curated set written to the user
   folder on first launch.
6. **Self-tests.** Section 10, both scenarios, verified clean via `cdb.exe`
   per this project's headless-verification convention.
7. **Polish.** Tick TODO.md item 9; if tempo-sync hasn't landed yet, no
   further action needed there (section 2's policy already covers that
   rename when it happens); if it has, confirm the rename's effect on old
   saves matches section 2's documented example.

---

## 12. Out of scope, explicit

- **`StepSequencer`'s own transient runtime state** (clock phase, gate-open
  flag) and **raw live-held-key state** — re-derived or genuinely transient,
  not parameters (section 2).
- **Embedding presets as `BinaryData`** — deferred to distribution time
  (section 1); revisit if/when `architecture.md`'s licensing section
  actually starts to matter.
- **Migration shims for renamed fields** — deliberately not built (section
  2); the accepted trade-off is silent loss, not silent-but-migrated.
- **Any change to `arpTempoBpm`/`seqTempoBpm` naming itself** — that's
  tempo-sync's job, not this item's; this doc's serializer is written to
  survive that rename via section 4's name-keyed mechanism regardless of
  which item lands first.
- **A `presetLoadInProgress` audio-thread gate** — considered and rejected
  in section 7, not merely unaddressed.

---

## Critical files

`Source/DSP/VoiceParameters.h`, `Source/UI/SynthPanel.h:289-304,471` +
`SynthPanel.cpp:109-265,861-868,928-933`, `Source/Arpeggiator.h:223,265,266,272`,
`Source/MainComponent.h:96-100` + `MainComponent.cpp:11-21,92-225,238-261,293-312`,
`Source/UI/ParameterControls.h:33-70,192-219`, `CMakeLists.txt:36-39,92-106`
(confirms `juce_data_structures` already linked — no build-system change
needed), new `Source/Presets/PresetSerialization.h/.cpp` (this item's own
addition).
