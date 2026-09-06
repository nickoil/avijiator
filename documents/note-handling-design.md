# Note handling design — MIDI, keyboard, priority, glide

Design record for **Stage A item 4** ([TODO.md](TODO.md)). Written before
implementation, same discipline as [dsp-voice-design.md](dsp-voice-design.md)
(item 2) and [envelope-lfo-design.md](envelope-lfo-design.md) (item 3).

Items 2 and 3 are closed. The voice makes the right sounds, but there is still no
way to *play* it: pitch comes from a debug slider, and the envelope is triggered by
a temporary Gate button. Item 4 replaces that scaffolding with real note input —
MIDI, computer keyboard, and an on-screen widget — plus the mono-synth behaviour
that makes overlapping notes musical: note-priority, glide, and legato/retrigger.

**Why this needs more than another atomic.** Item 3's Gate button works only
because pitch never changes at the same instant as gate. Real playing breaks that:
"release A while B is still held" must change pitch *and* keep the gate held, in the
same instant. Two independent atomics cannot guarantee the audio thread sees both
halves together — it can observe the new pitch with the old gate, or the reverse.
Hence a proper lock-free event queue.

### Why build this at all, when the arp and sequencer change notes anyway?

A fair question — the TB-303's keyboard is for pattern *programming*, not
performance, so it's tempting to skip straight to items 5 and 7. The answer is that
this item is mostly **not** "note input"; it's the machinery those two items are
built on:

- **The arpeggiator cannot exist without held notes.** [architecture.md](architecture.md)'s
  own arp section: *"maintain a sorted buffer of currently-held notes"*, then *"on
  each subdivision, advance through the buffer."* An arp doesn't generate pitches, it
  cycles pitches you are holding. It is a **consumer** of note input, not a substitute
  for it.
- **Glide and legato/retrigger *are* the 303's slide.** architecture.md, on the step
  sequencer: *"Slide: consecutive slide-flagged steps glide pitch instead of
  retriggering the envelope."* That is exactly `Glide` plus the legato path (section 4
  and 5 below). Item 7 cannot implement slide without them.
- **`noteOn`/`retargetPitch`/`noteOff` is the API items 5 and 7 call.**
  [dsp-voice-design.md](dsp-voice-design.md) section 6 already states they "own zero
  DSP and only call those methods at sample offsets." Without this item they have
  nothing to call.

Also worth noting the SH-101 — which architecture.md names first — has a full 32-key
keyboard *and* an arpeggiator that runs off held notes. Both reference instruments
support keeping this.

> **Model note**: CLAUDE.md names only items 2 and 5 for Opus, so item 4 defaults to
> **Sonnet**, same literal reading applied to item 3. Steps 1-3 (FIFO, priority
> stack, glide/legato) are flagged **Opus-available**: a wrong answer there is
> *silent* — a dropped event, a wrong fallback note — rather than a failure. That's
> a deliberate override, not a correction to the default.

---

## 1. Decisions and why

| Decision | Choice | Reasoning |
|---|---|---|
| Keyboard input | **Both** QWERTY-as-notes *and* an on-screen clickable widget, alongside real MIDI | All three feed one shared note path, so priority/glide/legato behave identically regardless of source |
| Event handoff | **Hand-rolled SPSC ring buffer**, not `juce::AbstractFifo` | Matches the project's hand-roll precedent (oscillator, filter, ADSR, LFO); avoids depending on JUCE semantics we can't verify without reading library sources |
| Note priority | Live-switchable enum, **default LastNote** | [architecture.md](architecture.md) leaves this explicitly open — "try both, keep whichever feels right by ear". Live-switchable is what makes that comparison possible without a rebuild. LastNote is the near-universal mono-synth default, including the SH-101 |
| Legato/retrigger | Live-switchable enum, **default Retrigger** | Keeps item 3's already-ear-verified behaviour as the default; Legato is the opt-in mode |
| Note event timing | Applied at **top of block**, not sample-accurate | Same granularity the existing gate edge-detection already uses. CLAUDE.md ties sample-accuracy specifically to item 5's arp clock; block boundaries (~5-11ms) sit well under human playing jitter |

### On the open note-priority decision

[architecture.md](architecture.md)'s open-decisions list says *"Note-priority scheme
for the mono voice (last-note vs. highest-note) — try both, keep whichever feels
right by ear."* Unlike the filter-topology decision, it states no starting point.

Per CLAUDE.md ("pick the stated starting point, say clearly that you did, and leave
it easy to swap"): **LastNote is the provisional pick, and it is a runtime switch,
not a hardcoded choice** — so the "try both" comparison is a combo-box change, not a
rebuild. The decision stays open until settled by ear.

### MIDI hardware — verification gap, and how it closed

`Get-PnpDevice -Class MEDIA` on this machine reports **`Focusrite Usb MIDI`**
already connected, so step 7's device enumeration and registration *is* verifiable
here even with no keyboard attached.

An **Edirol PCR-1**, if located, should also work over USB **with its rear "Advanced
Driver" switch set to OFF** — class-compliant mode, driven by Windows' built-in USB
MIDI driver. There is no Windows 11 Roland PCR driver; the Advanced-mode path is
dead on modern Windows. Its DIN out into the Focusrite's MIDI in is a second route.

*(Side note, not actioned: architecture.md's hardware section names the Scarlett
Solo, which has no MIDI I/O. This machine reports a Focusrite that does. Either a
different interface than the doc assumes, or the doc is out of date.)*

---

## 2. The event FIFO

### Two instances, not one — the multi-producer problem

Three input sources, but **two distinct producer threads**: QWERTY and the on-screen
widget both fire on JUCE's message thread (already serialized against each other),
while MIDI fires on its own MIDI thread. One SPSC queue cannot correctly serve two
producers, so there are **two instances of the same SPSC class**:

- `midiEvents` — MIDI thread → audio thread
- `uiEvents` — message thread (QWERTY *and* widget) → audio thread

This keeps the class genuinely single-producer rather than stretching it into
something it isn't, and the instance identity doubles as the source tag — no
`source` field needed on the event.

### The event

```cpp
struct NoteEvent
{
    enum class Type : std::uint8_t { NoteOn, NoteOff };
    Type type = Type::NoteOff;
    std::uint8_t noteNumber = 0;   // shared identity, so a NoteOff matches its NoteOn
    float pitchLog2Hz = 0.0f;      // converted at the producer, never on the audio thread
    float velocity = 0.0f;         // captured, unused in item 4
};
```

Pitch is `log2(Hz)`, matching the project's domain convention everywhere else
(`pitchLog2Hz`, `cutoffLog2Hz`). Conversion from MIDI note number happens at the
**producer** — `std::log2 (440.0f) + (noteNumber - 69) / 12.0f` — so the audio thread
never computes it.

### Capacity

**64**, power of two. The queue drains to empty every block; sustaining more than 64
events per block period would be ~6000 events/sec — not humanly reachable, and
indicative of a malfunctioning source (a MIDI feedback loop), where defined drop
behaviour beats a bigger buffer.

### Push/pop, with exact memory ordering

Indices are **monotonically increasing and never wrapped** — only `index & mask`
wraps. This avoids the classic head==tail ambiguity where empty and full look
identical.

```cpp
bool push (const NoteEvent& e) noexcept          // producer thread only
{
    const auto w = writeIndex.load (std::memory_order_relaxed);
    const auto r = readIndex.load (std::memory_order_acquire);

    if (w - r >= (unsigned) capacity) { jassertfalse; return false; }

    buffer[w & indexMask] = e;
    writeIndex.store (w + 1, std::memory_order_release);
    return true;
}

bool pop (NoteEvent& e) noexcept                  // audio thread only
{
    const auto r = readIndex.load (std::memory_order_relaxed);
    const auto w = writeIndex.load (std::memory_order_acquire);

    if (r == w) return false;

    e = buffer[r & indexMask];
    readIndex.store (r + 1, std::memory_order_release);
    return true;
}
```

**Why this ordering is correct.** Each index is written by exactly one thread, so
that thread's load of its *own* index is `relaxed` — nothing else writes it. The
load of the *other* thread's index is `acquire`, paired with that thread's `release`
store. The producer's acquire-load of `readIndex` (paired with the consumer's
release-store after finishing a slot) prevents overwriting a slot the consumer
hasn't finished reading on wraparound. The consumer's acquire-load of `writeIndex`
(paired with the producer's release-store after writing a slot) prevents reading a
slot whose write isn't yet visible. This is the standard SPSC ring-buffer pattern.

### Overflow

`push()` drops the **newest** event — the one being pushed — not the oldest. A lost
note-off is far worse than a lost note-on: it means a stuck note whose envelope never
releases. `jassertfalse` fires on the producer thread (message or MIDI), never the
audio thread, so it doesn't violate CLAUDE.md's audio-thread constraints.

---

## 3. Note priority

### Resolved on the audio thread — and this is load-bearing

The FIFO carries **raw** note-on/note-off events. `NoteStack` lives audio-thread-side
and is driven after draining. Two reasons this isn't merely a preference:

1. **Thread safety.** Resolving at production time would put the held-note stack on
   the producer side — shared between two producer threads — reintroducing exactly
   the shared-mutable-state race the FIFO exists to eliminate. Audio-side, the stack
   is single-thread-private, the same category as item 3's `lastGateState`.
2. **Live mode switching requires it.** If the queue only carried resolved "the note
   is now X" events, switching LastNote↔HighestNote mid-performance would have
   nothing to re-resolve from — the information about which *other* notes are still
   held was discarded upstream.

### Design

```cpp
enum class NotePriorityMode : int { LastNote = 0, HighestNote = 1 };

struct Resolution
{
    bool isSounding = false;      // false => stack now empty
    std::uint8_t noteNumber = 0;
    float pitchLog2Hz = 0.0f;
    float velocity = 0.0f;
};

Resolution noteOn  (std::uint8_t n, float pitchLog2Hz, float vel, NotePriorityMode) noexcept;
Resolution noteOff (std::uint8_t n, NotePriorityMode) noexcept;
```

Backed by a fixed `std::array<HeldNote, 16>` plus a count — **no allocation, ever**.

- **noteOn**: `removeIfPresent` first (so a re-press without a matching release
  refreshes recency rather than creating a duplicate entry), then append.
- **noteOff**: remove and **compact preserving press order** — that ordering is
  exactly what LastNote reads.
- **resolve**: LastNote returns the final entry; HighestNote scans for the highest
  note number; an empty stack returns `isSounding = false`.

**16, not 128**: no player holds more than ten fingers' worth of notes, and this is
scanned only on note events, never per-sample — so the headroom is free. A 17th
simultaneous note is dropped, never grown.

---

## 4. Glide / portamento

### Hand-rolled, for a specific reason

`juce::SmoothedValue::reset (sampleRate, rampTime)`'s snap-vs-not behaviour when
called mid-ramp is **unverified** — and per CLAUDE.md, JUCE sources are not to be
read to check. Hand-rolling sidesteps the question entirely and matches the
established precedent.

### Glide replaces the pitch smoother — it is not an additive offset

`SynthVoice`'s existing comment says *"Item 4's glide will add its offset here too"*
at the `pitchModulationOctaves` summing point. That marks the right **line** but the
wrong **model**, and it's worth correcting explicitly:

`pitchLog2Smoothed` is a 20ms anti-zipper ramp on a *debug slider* — it exists only
to stop a knob-turn clicking, and has nothing to do with musical portamento. Glide
**is** the note-to-note pitch transition, not a modulation offset layered on top of
one. Modelling it additively would mean implementing a ramp twice, awkwardly, to get
one result.

So `Glide` **replaces** `pitchLog2Smoothed` as the source of base pitch. The
insertion point stays exactly where the comment said:

```cpp
const auto pitchOctaves = glide.processSample() + pitchModulationOctaves;
```

and `VoiceParameters::pitchLog2Hz`, `pitchLog2Smoothed`, and the debug Pitch slider
are all deleted (see section 6).

### Constant rate, not constant time

**`glideTimeSeconds` means time-to-glide-one-octave** — a constant *rate* — not
time-to-complete-whichever-interval-is-in-flight.

This is what makes the parallel with `Adsr` actually hold. `Adsr::processSample()`
recomputes its increment fresh every sample from the live time constant, which works
because the envelope's range is fixed (0↔1). Pitch has no fixed range — the interval
between two notes varies per retarget — so the literal formula doesn't transfer. But
under a constant-rate reading, a knob move mid-glide changes only the *rate* of
future samples, needing **zero special-casing** — exactly like `attackSeconds`
changing mid-Attack. That's the strongest form of the parallel holding.

*(The alternative — locking in a distance-based increment at retarget so every glide
takes exactly `glideTimeSeconds` regardless of interval — would need explicit
handling for a mid-glide knob move, breaking the parallel.)*

**Consequence worth stating**: a one-octave glide takes longer than a semitone glide.
That's also how real analog portamento behaves (a CV ramp at fixed rate), so it's the
authentic choice, not merely the convenient one. Flagged as a by-ear tunable, same
tier as `maxFeedback` / `resonanceCompensation`.

`0` means instant/off, exactly. `minGlideSeconds = 0.001f` guards divide-by-zero,
mirroring `Adsr::minStageSeconds`.

### A first note from silence never glides

Real portamento circuits would happily slide from whatever the CV last sat at,
including a stale value from minutes ago — not what a player expects from the *first*
note of a phrase. So a note-on with nothing previously sounding calls
`snapToTarget()` immediately: instant, regardless of `glideTimeSeconds`. Glide only
glides on a genuine overlap. This is one of the concrete test recipes in section 9.

---

## 5. Legato vs Retrigger

### The trap in the pre-specified API

[dsp-voice-design.md](dsp-voice-design.md) section 6 pre-specified
`noteOn(pitchLog2Hz, velocity)` / `noteOff()` — written before legato/retrigger was
designed in detail. **A single `noteOn()` that decides retrigger-or-not from its own
gated state cannot implement this correctly.** It cannot distinguish:

- "a key went down" — subject to legato/retrigger, and
- "a key came up, revealing another key still held" — **never** a retrigger, in
  either mode

Both look identical from inside `SynthVoice` if only pitch and gated-state are
available. Collapsing them would make Retrigger mode re-pluck on every note-off
inside a held chord — silently wrong, exactly the failure class that's hard to catch.

So the interface needs a third **method**, not a third parameter:

```cpp
void noteOn (float pitchLog2Hz, float velocity) noexcept;  // a key went down
void retargetPitch (float pitchLog2Hz) noexcept;           // a key came up, another still held
void noteOff() noexcept;                                    // every key is now up
```

### Precise behaviour

| Event | Legato | Retrigger |
|---|---|---|
| Note-on, nothing held before | envelope retriggers, glide **snaps** | envelope retriggers, glide **snaps** |
| Note-on, something already held | glide **ramps**, envelope untouched | envelope **retriggers**, glide ramps |
| Note-off, stack still non-empty | pitch **ramps**, envelope untouched | **same** — never a retrigger |
| Note-off, stack now empty | `envelope.noteOff()` | same |

**Legato/retrigger governs note-ON only.** The third row is the one to get right in
both directions.

### Retrigger as the default

Beyond "simpler and more predictable": Retrigger is the direct continuation of what
item 3 already built and verified by ear — every note-on behaves like the Gate
button's press did. Defaulting to Retrigger means item 4 doesn't silently change
behaviour that's already been confirmed. Legato becomes the deliberately opted-into
mode. Same spirit as `envelopeDestination` defaulting to Amp: overlapping notes
silently *not* re-plucking, with no visual cue, is the more surprising default.

---

## 6. Removing the old scaffolding — a correctness requirement

The `gate` atomic, `GateButton`, `pitchLog2Hz` atomic, `pitchLog2Smoothed`, the debug
Pitch slider, and `lastGateState` are all **deleted**, not kept as fallbacks.

For the Pitch slider this is the established "delete superseded scaffolding"
precedent (item 2's 440Hz test tone was deleted outright). For the **gate**, there's a
sharper reason: keeping it would mean two independently edge-detected triggers of the
same envelope with no defined precedence — "gate=true from the button" versus
"gate=true from a held note" — reintroducing the exact race this item exists to fix.
Coexistence would be a correctness regression, not just untidiness.

`lastGateState` is replaced by `voiceGated` — same role, same audio-thread-private
treatment.

**Velocity is captured but unused**: a `currentVelocity` member with a comment
flagging it as item 7's accent hook and [character-and-vim.md](character-and-vim.md)
B5's velocity routing. Same pattern as item 2 pre-declaring the modulation summing
points at zero before item 3 filled them in.

---

## 7. Structure

```
Source/DSP/
  NoteEvent.h        new — event struct + NoteEventFifo + MIDI pitch helper
  NoteStack.h/.cpp   new — held-note set + priority resolution
  Glide.h            new — hand-rolled constant-rate pitch ramp
Source/
  NoteRouter.h/.cpp     new — owns both FIFOs + the stack, drives SynthVoice
  QwertyNoteInput.h/.cpp new — key map, octave shift, repeat-safe polling
```

*(Built header-only rather than the `Glide.h/.cpp` first sketched here: it turned
out to be a trivial per-sample kernel with no real setup logic, so it belongs in
`Vca.h`/`TptSvfStage.h`'s tier rather than `Vcf`'s.)*

### `NoteRouter` — the peer class, not part of `SynthVoice`

[dsp-voice-design.md](dsp-voice-design.md) section 6 states `SynthVoice` knows
nothing about MIDI, timers, tempo or patterns, and that items 5/7 become "peer
classes that own zero DSP." A live-input note router is exactly that kind of class,
just block-granular rather than sample-accurate. It lives at `MainComponent`'s level,
owned alongside `voice`:

```cpp
void dispatchPendingEvents (SynthVoice&, NotePriorityMode) noexcept;  // audio thread, top of block
```

The mapping is nearly mechanical:

- `NoteStack::noteOn` resolution → `voice.noteOn(...)`
- `NoteStack::noteOff` with `isSounding == true` → `voice.retargetPitch(...)`
- `NoteStack::noteOff` with `isSounding == false` → `voice.noteOff()`

### A note-on that changes nothing must not reach the voice

**Found during implementation; not in the original design.** `NoteRouter` tracks
which note is *actually sounding*, separately from which notes are *held*, and
suppresses a `noteOn` that wouldn't change it:

```cpp
if (! voiceIsSounding || resolution.noteNumber != soundingNoteNumber)
    voice.noteOn (resolution.pitchLog2Hz, resolution.velocity);
```

Without this, Retrigger mode re-plucks the envelope for a keypress that changed
nothing audible. It happens for real in **HighestNote** mode: hold G, press a
lower C, and G keeps sounding — the C is swallowed by the priority rule, so an
envelope thump would be an artifact with no cause the player can hear. (LastNote
never hits it, since a new press always wins.)

The same comparison guards `retargetPitch` on the note-off path.

**Known limitation, stated rather than glossed**: draining `midiEvents` fully before
`uiEvents` means cross-source ordering *within a single block* isn't preserved
(within-source ordering always is, since each FIFO is strictly ordered). Low-stakes
at block granularity, but real.

### QWERTY layout

| Key | `Z` | `S` | `X` | `D` | `C` | `V` | `G` | `B` | `H` | `N` | `J` | `M` |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Note | C | C# | D | D# | E | F | F# | G | G# | A | A# | B |

| Key | `Q` | `2` | `W` | `3` | `E` | `R` | `5` | `T` | `6` | `Y` | `7` | `U` |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Note | C | C# | D | D# | E | F | F# | G | G# | A | A# | B | *(one octave up)* |

**Octave shift: `,` down, `.` up** — deliberately *not* Z/X, which are already note
keys in this layout (an easy collision to inherit by copying the pattern from
memory). `Z` = C3 (MIDI 48); clamp the shift to roughly [-2, +4] to stay inside
MIDI's 0-127 range.

**Key-repeat immunity by construction.** Whether JUCE's key callbacks re-fire on OS
auto-repeat is unverified. Rather than depend on it: keep a `bool keyIsHeld[]` array
and, on any key-state change, re-poll each mapped key and **diff against stored
state**. A repeat event corresponds to a key already recorded as held → no transition
→ no duplicate note-on. Correct regardless of the underlying repeat semantics.

### On-screen widget — hand-rolled, not `MidiKeyboardComponent`

A row of 13 `juce::TextButton`s using the **exact `mouseDown`/`mouseUp` override
pattern `GateButton` already proved** in item 3 (generalised to `MomentaryButton`
during step 3, and reused unchanged here).

`juce::MidiKeyboardComponent` + `MidiKeyboardState` is a real, well-known widget, but
it's a comparatively large unverified surface (`MidiKeyboardStateListener`,
click-position velocity, scrolling) to bridge into the FIFO — for something item 6
throws away entirely. Reusing known-good code beats introducing a new unverified API
for disposable scaffolding. Fixed range (C3-C4) at the time of writing.

**Update, item 6 UI pass:** the real (non-disposable) on-screen keyboard's key
captions show the matching QWERTY letter, which only stays honest if both
inputs share one octave shift. So `SynthPanel` grew its own Octave Up/Down
buttons next to the keyboard, but they don't keep separate state - they call
into `QwertyNoteInput::octaveUp()/octaveDown()` (the same clamped adjustment
comma/period make) and `MainComponent` mirrors the result back with
`SynthPanel::setOctaveShift()`, so a button click and a comma/period press
move the same one shift.

**Update, later revision: octave promoted to one shared, global "note output"
transpose.** The scheme above shifted notes at *generation* time, and only
for keyboard-originated ones — `QwertyNoteInput` baked the shift into the
MIDI note number it computed for QWERTY presses, `SynthPanel`'s on-screen
keyboard duplicated that arithmetic against its own mirrored (explicitly
not-source-of-truth) copy, and MIDI hardware input, the arpeggiator, and the
step sequencer never saw the shift at all.

Following the exact shape of decision `tempo-sync-design.md` made for
`masterTempoBpm` — one shared dial, not a per-consumer copy — the shift is
now `VoiceParameters::masterOctaveShift`, one atomic. `QwertyNoteInput`'s
comma/period handling calls `adjustMasterOctaveShift()` to move it rather
than owning any state of its own, and `QwertyNoteInput`/the on-screen
keyboard now emit raw, untransposed note numbers.

The transpose itself moved downstream, to the point each of the three note
sources hands its final pitch to `SynthVoice`:

- **Keys** (QWERTY, on-screen keyboard, **and MIDI hardware input** — a
  deliberate scope widening, not an oversight): `NoteRouter::apply()` and
  `NoteRouter::retakeVoice()` add the shift to `resolution.pitchLog2Hz`
  before calling `voice.noteOn`/`retargetPitch`. Both MIDI and UI note
  events drain through the same `apply()`, so this one change point covers
  all three keyboard-shaped sources at once — no change needed to
  `MainComponent`'s MIDI handling.
- **Arp**: `Arpeggiator::process()` adds the shift to the pitch it passes to
  `voice.noteOn`.
- **Seq**: `StepSequencer::process()` adds the shift where it reads back a
  step's stored pitch for playback. The *recorded* pitch itself
  (`stepPitchLog2Hz`) stays untransposed, sourced straight from `NoteStack` —
  so a recorded step picks up whatever the current transpose is at playback
  time, same as a hand-entered one.

Because the transpose is applied fresh at each of these call sites rather
than stored per held note, two behaviors fall out with no special-casing,
and are the settled, by-design result rather than something to fix later:

- A note already sustaining does not re-pitch the instant the control is
  touched — it picks up the new shift on the *next* voice call for it (next
  key transition for Keys, next step for Arp/Seq).
- A fallback retarget (releasing a top note reveals a lower held one) uses
  the shift in effect *at the moment of the fallback*, not the one in effect
  when that note was originally pressed.

The Octave control itself also moved, from its old freestanding spot next to
the on-screen keyboard into the OUTPUT panel section, alongside Level and
Tempo — the same "global control, not owned by one input source" reasoning
`tempo-sync-design.md` used to relocate Tempo out of ARP.

**Update, follow-up visual pass:** the relocated control's first cut was a
literal port of the old widget - two `TextButton`s and a text readout,
just stacked into a narrower `PanelSection` cell than the freestanding
space it used to have. Cramped and visually inconsistent with OUTPUT's
other two controls (both plain rotary knobs), so it was rebuilt as a
`KnobCell` - a detented rotary slider (`setRange`'s third argument, the
interval, forces whole-octave steps rather than a continuous drag) with a
read-only text box showing a signed integer ("+1", "-2", "0") via `Slider::
textFromValueFunction` - a transpose control reads as "how far from normal"
more directly than an absolute note name would - matching Level/Tempo's
look exactly. `Slider::updateText()` is called once explicitly after
seeding, since `setValue` skips `updateText()` when the value doesn't
actually change (true at startup, when the slider's own default already
equals `masterOctaveShift`'s default of 0) - without it the text box would
show its raw pre-`textFromValueFunction` default until the first drag.
Hand-wired
directly in `SynthPanel`'s constructor rather than through the generic
`KnobSpec`/`attachKnob`/`wireKnobs` path every other knob in the panel goes
through - `masterOctaveShift` is an `atomic<int>`, not the `atomic<float>`
that mechanism's `KnobSpec::target` requires, and its readout is a lookup
("C3") rather than a numeric value plus suffix. Same "doesn't fit the
generic shape, hand-wire it" precedent `seqPatternLengthCombo`/
`seqLaneCombo` already set. Comma/period still work exactly as before -
they drive the same shared atomic, and `MainComponent::keyStateChanged`'s
call to `SynthPanel::refreshOctaveReadout()` now moves the knob's position
(via `Slider::setValue`) instead of a label's text.

---

## 8. `VoiceParameters` additions

| New | Type | Default | Treatment |
|---|---|---|---|
| `notePriorityMode` | `atomic<int>` | LastNote | Discrete, no smoother — matches `envelopeDestination` |
| `legatoRetriggerMode` | `atomic<int>` | Retrigger | Discrete, no smoother |
| `glideTimeSeconds` | `atomic<float>` | 0.0 (off) | **Time constant**, no smoother — matches the ADSR times and `lfoRateHz` |

All three confirmed against the convention established in
[envelope-lfo-design.md](envelope-lfo-design.md) section 5, not assumed to match it.
`glideTimeSeconds` qualifies as a time constant specifically because `Glide`'s
constant-rate design means a live change alters only the rate of future samples,
never the current position — which is the exact test that separates a "time
constant" from a "directly assigned value."

Debug UI: two new `DebugChoiceSpec` rows ("Note Priority", "Glide Mode") and one new
`DebugControlSpec` row ("Glide Time", 0-5s, default 0) — extending the existing
tables exactly as that pattern intends.

---

## 9. Build order

QWERTY lands before MIDI and the widget — cheapest to test, needs no hardware, and it
proves the whole FIFO→stack→voice pipeline before a second input source exists.

| # | Name | Model | Work | Ends with |
|---|---|---|---|---|
| **0** | **Design** | Sonnet | This document + `architecture.md` / `TODO.md` links. **No code.** | Design captured before implementation |
| 1 | **Fifo** | Sonnet, *Opus available* | `NoteEvent.h` — struct + `NoteEventFifo`, header-only. Debug-only startup self-test: scripted push/pop, `jassert` round-trip correctness. | FIFO provably correct in isolation, before anything depends on it |
| 2 | **Stack** | Sonnet, *Opus available* | `NoteStack.h/.cpp`. Debug-only self-test running section 10's recipes, asserting expected resolutions in **both** priority modes. | Priority algorithm provably correct in isolation |
| 3 | **Voice** | Sonnet, *Opus available* | `Glide.h`; `noteOn`/`retargetPitch`/`noteOff`; `legatoRetriggerMode`. **Deletes** the Pitch slider, `pitchLog2Hz`, `pitchLog2Smoothed`, `gate`, `GateButton`, `lastGateState`. Two debug note buttons. | Buttons prove glide + legato/retrigger before any real input source exists. **The buttons had to LATCH** — a mouse has one pointer, so momentary ones could never be held together, which is exactly what these tests need |
| 4 | **Router** | Sonnet | `NoteRouter.h/.cpp`, `notePriorityMode` + combo. | Releasing one held note falls back to the other instead of cutting out. **Added beyond plan**: suppressing a note-on that wouldn't change the sounding note (see section 7) |
| 5 | **Qwerty** | Sonnet | `QwertyNoteInput` — full key map, octave shift, repeat-safe polling. | **First fully playable milestone**, no hardware required |
| 6 | **Widget** | Sonnet | On-screen button row → the same `uiEvents` FIFO. | Second input path proven, cheap once everything else works |
| 7 | **Midi** | Sonnet | `MidiInputCallback`, `addMidiInputDeviceCallback` → `midiEvents`. | Builds, registers, converts correctly — but **never tested against hardware**, see section 10 |
| 8 | **Polish** | Sonnet | Comment pass; reconcile this document; tick TODO item 4. | Item 4 closed |

### Detours taken during the build, not in the original plan

Recorded because none of it is discoverable from the plan alone:

- **ASIO enabled** (`JUCE_ASIO=1`, Windows-only) after the latency proved bad
  enough to interfere with playing. JUCE 9 bundles the Steinberg headers, so no
  SDK download — but see `architecture.md`'s new Licensing section.
- **Audio Settings panel** (`juce::AudioDeviceSelectorComponent` in a dialog).
  Nothing previously let a device, driver type or buffer size be chosen at all.
  It also carries the MIDI input list, which step 7 then needed anyway.
- **`CMakePresets.json`** pinning `toolset host=x64`. The 32-bit hosted `cl.exe`
  ran out of heap space (`error C1060`) once ASIO forced a full JUCE rebuild.
  `-A x64` sets the *target* architecture, not the compiler's own — a latent
  problem that had simply never been triggered before.
- **Two-column layout**, 900×560. The single column was already overflowing:
  21 control rows plus buttons needed ~690px inside a 680px area, so the note
  buttons were being pushed off-screen. Buttons are now reserved from the bottom
  *before* rows are laid out, so no future control count can repeat it.
- **`CLAUDE.md`'s Build/Run section filled in** — it had been `_TBD_` since
  tooling setup, meaning a fresh session had no way to build the project.

### How to run a step

```
Build Stack from documents/note-handling-design.md
```

Same instruction pattern as items 2 and 3 — names and numbers are interchangeable.

---

## 10. Verification

### Claimable without ears

Builds clean (Debug + Release, zero new warnings). Launches, no crash, no stderr.
**Three** Debug self-tests pass at startup (`jassert` never fires):

| Self-test | Covers |
|---|---|
| `runNoteEventFifoSelfTest` | Round-trip fidelity, FIFO ordering, exactly-full boundary, wraparound past capacity |
| `runNoteStackSelfTest` | Both priority modes against the recipes below; re-press refreshes rather than duplicates; unheld note-off is a no-op; 17th note dropped without corruption |
| `runMidiConversionSelfTest` | Note-on/off conversion, pitch domain, **note-on-with-velocity-0 treated as note-off**, non-note messages rejected |

None of these can exercise the FIFO's *memory ordering* — no single-threaded test
can. That rests on the reasoning in section 2.

### The MIDI gap — what actually shipped untested

**Step 7 was never tested against physical MIDI hardware**, because none was
available. Splitting the message conversion out of the callback narrowed the gap
considerably, but did not close it:

| | Status |
|---|---|
| Message → `NoteEvent` conversion, incl. running status | **Verified** by self-test |
| Non-note messages ignored | **Verified** |
| Builds, launches, registers the callback without crashing | **Verified** |
| A physical key actually producing sound | **UNVERIFIED** |

So the residual risk is device enumeration and callback registration — plumbing
that would fail loudly rather than subtly — not the conversion logic. Worth
re-checking the moment any MIDI device is to hand.

### Needs a human at the speakers — concrete recipes

- **Priority, differentiating**: hold E; while still held, press C (lower).
  *LastNote*: pitch switches to C. *HighestNote*: stays on E. Distinguishes the two
  modes without even releasing anything — do this one first.
- **Priority, must-not-go-silent**: hold C4, then G4, then E4 — all three down,
  pressed in an order where recency and pitch deliberately disagree.
  *LastNote* sounds **E4** (most recent); *HighestNote* sounds **G4** (highest)
  from the outset. Release E4 → LastNote falls back to **G4 by recency, not
  pitch**; HighestNote is unchanged, since E4 was never sounding. Release G4 →
  both fall to C4. Release C4 → silence. Confirms releasing the sounding note
  never goes silent while others are held.
- **Legato vs Retrigger**: glide off, Retrigger mode. Hold C, note the attack
  transient; press E while C is still held → expect a **second** audible attack.
  Switch to Legato, repeat → expect **no** second attack. *Edge case*: in Legato,
  play a note from complete silence → it **should** still retrigger normally, which
  checks the fresh-trigger path isn't wrongly suppressed.
- **Glide, first-note-never-glides**: Glide Time 1s. Play a note, release it fully to
  silence, then play one an octave higher → expect **no** audible slide. Catches a
  naive implementation sliding up from a stale prior pitch.
- **Glide, overlap**: same settings. Hold C, then press E an octave up while C is
  still held → expect an audible ~1s slide.
- **Glide, live-adjust mid-ramp**: start a long glide (3s) between two notes; while
  the slide is audibly still moving, drag Glide Time down to ~0.1s → the remaining
  distance should complete much faster with **no click or discontinuity**. This makes
  the "time constant" claim concrete and checkable, mirroring item 3's retrigger-click
  test.

### Taste — the developer's call alone

Which priority mode feels right (this settles architecture.md's open decision by ear);
whether Retrigger-as-default was the right call; glide feel and whether constant-rate
is preferable to constant-time; QWERTY layout comfort; whether the 16-held-note cap is
ever felt (it shouldn't be).

---

## 11. JUCE APIs — flagged, then resolved

Every API flagged as moderate-confidence during design **compiled correctly with
the assumed signature**, first try:

- `juce::Component::keyStateChanged (bool)`, `juce::KeyPress::isKeyCurrentlyDown
  (int)`, `Component::setWantsKeyboardFocus (bool)`, `Component::focusLost
  (FocusChangeType)` ✓
- `juce::MidiInputCallback::handleIncomingMidiMessage`,
  `juce::MidiInput::getAvailableDevices()`,
  `AudioDeviceManager::addMidiInputDeviceCallback` /
  `removeMidiInputDeviceCallback` / `setMidiInputDeviceEnabled` ✓
  (`juce_audio_devices` was already linked, so no module change was needed.)
- `juce::AudioDeviceSelectorComponent`'s 9-argument constructor and
  `juce::DialogWindow::LaunchOptions` ✓ — added during the latency detour.

Two things were deliberately *not* left to trust, and both still stand:

- **MIDI running status** is handled defensively rather than depending on
  `isNoteOn`/`isNoteOff`'s default arguments: NoteOn only when
  `isNoteOn() && getVelocity() > 0`; NoteOff when
  `isNoteOff() || (isNoteOn() && getVelocity() == 0)`. Correct either way, and
  now covered by a self-test.
- **Key auto-repeat** is made irrelevant by diffing a fresh poll rather than
  trusting how JUCE surfaces repeats.

`addMidiInputDeviceCallback` is registered with an **empty device identifier**,
which acts as a catch-all across every enabled input — so devices switched on
later in the settings panel are picked up without re-registering. That behaviour
is assumed, and is part of what the untested hardware path would confirm.

---

## 12. Out of scope

Not in item 4: the arpeggiator and its sample-accurate clock (item 5), accent/slide
(item 7), velocity *routing* (captured but unused here), exponential envelope curves
or any [character-and-vim.md](character-and-vim.md) item, polyphony (forbidden by
CLAUDE.md), MPE, and MIDI CC / mod-wheel routing (character-and-vim.md B5, Tier 3).
