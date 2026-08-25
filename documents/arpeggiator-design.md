# Arpeggiator design — clock, patterns, hold

Design record for **Stage A item 5** ([TODO.md](TODO.md)). Written before
implementation, same discipline as [dsp-voice-design.md](dsp-voice-design.md)
(item 2), [envelope-lfo-design.md](envelope-lfo-design.md) (item 3) and
[note-handling-design.md](note-handling-design.md) (item 4).

Items 2–4 are closed: the voice makes the right sounds and can be played from
MIDI, the computer keyboard and an on-screen keyboard. Item 5 makes it play
*itself* — hold a chord, and the arp cycles through it on a musical clock.

**Two things were designed in earlier specifically for this moment**, and should
be cashed in rather than worked around:

- **`SynthVoice::renderNextBlock (float*, int)`** takes a raw pointer and count
  precisely so item 5 can call it several times per block with advancing
  pointers. [dsp-voice-design.md](dsp-voice-design.md) section 6 says so
  explicitly. No signature change is needed.
- **`NoteStack` already maintains the held set in press order**, capped at 16,
  allocation-free. That press ordering exists because LastNote priority reads it
   — and it is exactly what an "as-played" arp pattern needs. Reuse it; do not
  build a second held-note structure.

> **Model note**: unlike items 3 and 4, item 5 **is** on CLAUDE.md's Opus list,
> and the reason is the clock: a timing bug here is *inaudible* (see section 12),
> so it cannot be caught by ear at all. Steps 1, 2, 3 and 6 are the Opus ones.

---

## 1. Decisions and why

| Decision | Choice | Reasoning |
|---|---|---|
| Clock | **BPM + subdivision**, not a free-running Hz rate | Item 7's step sequencer shares this clock and needs musical divisions; swing ([character-and-vim.md](character-and-vim.md) B2) is defined per-subdivision. Costs SH-101 literalism, which had no tempo concept |
| Hold / latch | **In scope** | The SH-101 has it, and it frees both hands for the filter — the point of a live instrument |
| Octave range | **Out of scope** | Deferred deliberately, not forgotten |
| Arp + glide | **Accept that glide and legato go inert while arping; document it** | See section 10 — each step re-triggers from silence, so pitch snaps. This is the correct *default*: a 200 ms glide at 1/16 is a siren, not an arpeggio |
| Up-down endpoints | **Not repeated** — C E G E C | The two-note case decides it: repeating gives C C E E, a doubled trill that reads as a bug. Recorded as taste in section 12; two lines to flip |
| Hold add-vs-replace | **Replace** | See section 9 — and it reproduces both hardware behaviours anyway |

---

## 2. `StepClock`

`Source/DSP/StepClock.h`, header-only. Lives in `Source/DSP/` for the same
reason `NoteEvent.h` and `NoteStack.h` do: that directory is really
"audio-thread primitives", not strictly DSP.

**This is the piece item 7 inherits.** The sub-block loop is *not* — item 7 will
write its own ~25-line loop with a different step action (pattern position,
accent, slide). Abstracting the loop behind a base class before there are two
real users would cost more than the duplication. Deliberate call, recorded so it
does not look like an oversight later.

### Subdivision: an enum plus a table, not a multiplier

Triplets are 1/3, which no power-of-two multiplier reaches — and the UI needs
display names anyway, so the table earns its place twice.

```cpp
enum class StepDivision : int
{
    Quarter = 0, QuarterTriplet, Eighth, EighthTriplet,
    Sixteenth, SixteenthTriplet, ThirtySecond
};

// BEATS per step, where one beat is a quarter note. Ordered longest-first so
// the combo box reads slow -> fast.
constexpr double table[] = { 1.0, 2.0/3.0, 0.5, 1.0/3.0, 0.25, 1.0/6.0, 0.125 };
```

The lookup is **bounds-checked**. Not paranoia: the index arrives from a
`std::atomic<int>` the message thread writes, so a stale or out-of-range value
must not index out of the table on the audio thread. Same defensive posture as
`noteEventFromMidiMessage` not trusting `isNoteOn`'s defaults.

### Samples per step

```
samplesPerStep = sampleRate * (60.0 / bpm) * beatsPerStepForDivision (division)
```

All `double`. At 44100 Hz / 120 BPM / 1/16 this is exactly **5512.5** — a
half-sample, and the entire reason the fractional remainder has to survive.

### The three things that carry correctness

```cpp
void advance (int n) noexcept  { samplesUntilNextStep -= (double) n; }

void advanceStep() noexcept    { samplesUntilNextStep += samplesPerStep; ++stepIndex; }

int getSamplesUntilNextStep() const noexcept
{
    return samplesUntilNextStep <= 0.0 ? 0 : (int) (samplesUntilNextStep + 0.5);
}
```

1. **`+=`, never `=`** in `advanceStep`. The fractional remainder of the previous
   step must carry into the next, or the clock drifts. With `samplesPerStep =
   5512.5` the fire offsets alternate 5513, 5512, 5513… and average exactly
   5512.5. Assigning instead truncates half a sample every step — ~8
   samples/second at 1/16 120 BPM, 36 ms over a three-minute jam — and would be
   **completely inaudible while being wrong**.
2. **Rounding is a read-only projection, never fed back.** Only the `double`
   accumulator advances, so rounding errors cancel instead of piling up. Rounded
   to *nearest* rather than truncated, halving worst-case error to half a sample
   (11 µs at 44.1 kHz).
3. **A tempo change does not rescale the in-flight step.**

### Why no rescale is right — and why it needs no code

This is the same question the ADSR times, `lfoRateHz` and `glideTimeSeconds` all
answer, and the answer falls out of the *representation*, not from special-casing:

| Representation | Behaviour on tempo change |
|---|---|
| **Countdown in samples** (chosen) | Current step keeps its old length; the new length applies from the next step |
| Phase 0..1 + increment | Remaining time rescales proportionally — the in-flight step changes length under you |

The countdown matches every other time constant in the project. One knob-drag on
tempo therefore behaves exactly like one on Glide Time: continuous, click-free,
lagging by at most one step.

**One clamp, and it is a usability valve rather than a maths one:**
`samplesUntilNextStep = jmin (samplesUntilNextStep, samplesPerStep)`. Without it,
a big tempo *increase* would stall for up to a whole old step — four seconds at
20 BPM — before anything responded. In the steady state the countdown is always
≤ `samplesPerStep`, so this is a no-op and calling `setTempo` every block is free.

`getStepIndex()` is free-running and never wrapped. Item 7 takes
`stepIndex % patternLength`; character-and-vim B2's swing takes its parity from
here — which is why the counter belongs to the clock rather than the arpeggiator.

---

## 3. The sub-block render loop

### Two independent deadlines, not one alternating countdown

With `gateSamples < samplesPerStep` the note-on and note-off streams *do* strictly
alternate, and a single countdown-plus-a-flag would suffice. It is still written
as **two deadlines, take the min**, because the alternation invariant breaks in
three reachable cases — one of which is a stuck note forever:

1. **All keys released mid-step.** The next step has no note to play, so there is
   no note-on — but the *previous* step's gate-off is still pending. An
   alternating countdown either skips it (**stuck note forever**, the worst
   failure mode here) or fires a phantom. Two deadlines handle it because the
   gate-off is not conditioned on there being a note.
2. **Tempo raised mid-gate.** `gateSamples` is fixed at note-on; the clamp above
   can then pull the next step boundary in front of the pending gate-off.
3. **A future Tie mode** (section 10) deliberately holds the gate across a
   boundary.

Cost: about four lines. It is also where the ordering rule lives explicitly.

```cpp
while (offset < numSamples)
{
    const int remaining = numSamples - offset;
    const int toStep    = clock.getSamplesUntilNextStep();
    const int toGateOff = gateIsOpen ? samplesUntilGateOff : remaining;

    // CLOSE BEFORE OPEN. A gate-off and a step landing on the same sample must
    // not note-off the note that just started.
    if (gateIsOpen && toGateOff <= 0)
    {
        voice.noteOff();
        gateIsOpen = false;
        continue;                    // this sample may ALSO be a step boundary
    }

    if (toStep <= 0)
    {
        const auto index = chooseNextIndex (active, pattern);

        if (index >= 0)
        {
            voice.noteOn (active[index].pitchLog2Hz, active[index].velocity);
            lastNoteNumber = active[index].noteNumber;
            samplesUntilGateOff = /* re-derived from the CURRENT step length */;
            gateIsOpen = true;
        }

        clock.advanceStep();         // advances even when nothing played, so the
        continue;                    // grid keeps phase while the set is empty
    }

    const int chunk = juce::jmin (remaining, toStep, toGateOff);
    jassert (chunk > 0);

    voice.renderNextBlock (output + offset, chunk);
    clock.advance (chunk);
    if (gateIsOpen) samplesUntilGateOff -= chunk;
    offset += chunk;
}
```

### Termination is provable, not hoped for

Every branch strictly consumes its event: closing the gate clears `gateIsOpen`
(so it cannot re-fire until a step reopens it), and `advanceStep()` adds
`samplesPerStep ≥ 16`. Gate is clamped to `[1, stepSamples-1]`. So every path
leaves both deadlines ≥ 1, `chunk ≥ 1`, and `offset` strictly increases.

A **step landing exactly on a block boundary** gives `toStep == 0` and takes the
`continue` — an *event* is consumed, not a zero-length render issued. The
`jassert (chunk > 0)` makes the invariant checkable rather than merely argued.

**Steps per block are bounded** by `numSamples / minSamplesPerStep + 2`. The
fastest musical step is 300 BPM / 1/32 / 44.1 kHz = 1102 samples, so typical
buffers see 0 or 1. The `minSamplesPerStep = 16` floor exists so that a *future
parameter-range mistake* cannot degrade the loop into per-sample calls.

### Opposite rounding treatments, deliberately

> The step clock's remainder is **fractional and carried forward**; its rounding
> is a read-only projection never fed back. The gate's remainder is **integral and
> re-derived from scratch every step**, so its rounding error resets each step
> instead of accumulating.

Opposite treatments for opposite reasons. Swapping them is exactly the silent bug
this design is avoiding.

### Gate is clamped to 0.05–0.95, and 100% is unreachable

`jmax (1, …)` is load-bearing for **termination**, not just musicality: a
zero-length gate would open and close on the same sample forever without ever
rendering.

Gate = 100% is deliberately not reachable. It is not "a longer gate" — it changes
the *semantics* of a step from a fresh press into a legato retarget, which is a
different feature (section 10), and it is what would make glide audible. That
belongs with item 7's slide flag or an explicit follow-up, not smuggled in at the
top of a slider.

---

## 4. Pattern walking — a comparison key, not an index

**The central idea.** The held set changes size between steps. An index into a
shrinking array is ill-defined; a **comparison key is always well-defined**. So
the walker's entire state is:

```cpp
int  lastNoteNumber   = -1;   // int, not uint8_t, so "none yet" is representable
bool goingUp          = true; // UpDown only
bool patternIsRunning = false;
```

and each step asks a *query* about the current set rather than incrementing a
cursor:

| Mode | Query |
|---|---|
| Up | smallest held note strictly **>** `lastNoteNumber`, else wrap to lowest |
| Down | largest held note strictly **<** `lastNoteNumber`, else wrap to highest |
| UpDown | the current direction's query; if it fails, flip direction and ask the other |
| AsPlayed | position of `lastNoteNumber` in **press order**, +1, modulo count |
| Random | uniform over the notes that are not `lastNoteNumber` |

Everything awkward then falls out **free**:

- Releasing the note you just played is harmless — `lastNoteNumber` is only a
  key, it need not still be held.
- A note added mid-pattern lands in **pitch position** on the very next step, not
  appended at the end.
- Changing pattern mid-run needs no re-initialisation.
- No index ever needs clamping, so the classic *"released a note and the arp
  skipped or stuttered"* bug is structurally impossible.

Ordering compares **`noteNumber` (an int), never `pitchLog2Hz` (a float)**. They
are monotonically related so it is the same order, `NoteStack` de-dupes note
numbers so ties are impossible, and there is no reason to take a float-comparison
risk. Cost is O(n) per query with n ≤ 16, roughly 20 times a second — nothing.

**First step of a phrase starts at a defined end** rather than from a
`lastNoteNumber` that may be minutes old — the same reasoning as `SynthVoice`
snapping the pitch on a fresh trigger from silence.

**UpDown's double-failure case is a proof, not a guess**: both queries failing
means every candidate compares equal to `lastNoteNumber`, which — since
`NoteStack` de-dupes — can only be "exactly one note is held and it is the one
just played". Repeat it. That is why it never recurses and never flips forever.

### Random

Reuses `NoiseGenerator` via a new `nextUInt32()`, split out of `processSample()`.
This is a **behaviour-identical refactor** — same state advance, same bit
extraction — so the audible noise source's sequence does not move and the
existing A/B baseline is unchanged. Worth saying, because "the noise sounds
different now" would be a nasty surprise.

The arp holds its own `NoiseGenerator` with a distinct seed, which is exactly why
that class already takes one.

**No immediate repeats**, done by *excluding* the last note rather than
re-rolling: an unbounded reject-and-retry loop does not belong on the audio
thread. Side effect worth advertising: with exactly two notes held, this
degenerates to strict alternation — which makes the rule ear-checkable in three
seconds.

---

## 5. Exposing the held set

Minimal change to `NoteStack`: move `struct HeldNote` from private to public (it
is a POD, exactly like the already-public `Resolution`) and add two accessors.

```cpp
// A READ-ONLY view, in PRESS ORDER, oldest first. Const, so the arpeggiator can
// walk the set it is driven by but can never mutate it - the stack stays owned
// by NoteRouter and only NoteRouter.
std::span<const HeldNote> getHeldNotes() const noexcept;

// Re-resolve priority against the CURRENT set without a note event happening.
// Used when the arpeggiator hands the voice back: the keys are still down, so
// something must sound again without waiting for the next key press. A one-line
// wrapper over the existing private resolve() - no second copy of the rule.
Resolution getCurrentResolution (NotePriorityMode mode) const noexcept;
```

A span rather than a copy: sixteen notes is small, but this is read at every arp
step on the audio thread and copying would be pure ceremony.

`std::span` is C++20 standard library (already set) rather than a JUCE
dependency. It is the one *new* standard-library dependency this item introduces;
if it ever trips the Stage B NDK toolchain, the fallback is a pointer+count pair
or an indexed accessor with a Debug bounds `jassert`, and the walker changes in
one line.

`runNoteStackSelfTest` gains an assertion that after `press(C4); press(G4);
press(E4); noteOff(G4)` the span is `{C4, E4}` in that order — asserting the
press-order guarantee the arp now depends on, rather than leaving it a comment.

---

## 6. Architecture and integration

`Arpeggiator` is a **peer class owned by `MainComponent`**, exactly as
[dsp-voice-design.md](dsp-voice-design.md) section 6 specifies for items 5 and 7.
It owns a `StepClock`, a `NoiseGenerator`, the walker state and the latched set.
It owns no DSP.

**`NoteStack` stays inside `NoteRouter`.** The arp only ever *reads* it; moving
it out would give audio-thread-private state two owners and buy nothing.

`NoteRouter` gains:

```cpp
enum class VoiceDrive : int { Direct = 0, TrackOnly = 1 };

void dispatchPendingEvents (SynthVoice&, NotePriorityMode, VoiceDrive) noexcept;
const NoteStack& getNoteStack() const noexcept;
void releaseVoice (SynthVoice&) noexcept;                      // idempotent
void retakeVoice (SynthVoice&, NotePriorityMode) noexcept;
```

In `TrackOnly`, `apply()` still runs `noteStack.noteOn/noteOff` — **held-note
tracking stays live in both modes**, which is precisely what makes the transitions
cheap — but issues no `voice.*` call and does not touch `voiceIsSounding`. Those
fields are then only ever written by whichever side actually owns the voice.

### The hand-over invariant

> **Whichever side stops driving the voice leaves it silent, and whichever side
> takes over starts from silence.**

Both `releaseVoice` methods are idempotent, so both can be called on every
transition and exactly one actually does anything. That is what makes a stuck
note **unreachable by any toggle order**, rather than merely unlikely.

With the arp **off**, the audio path is byte-identical to item 4's — no
behavioural or performance change to the existing instrument.

---

## 7. Transitions — the part that ruins a live instrument

| # | Situation | Without handling | Handled by |
|---|---|---|---|
| T1 | Arp ON while a note is held and sounding | The router's sustained note never gets a note-off; its belief goes stale, so a later arp-OFF gives **silence with a key held** — a "stuck off" note, as bad as stuck-on | `router.releaseVoice`, then `arp.releaseVoice` (which also parks the clock at zero so the first step fires at once) |
| T2 | Arp OFF mid-step with a gate open | Gate never closes → sustains forever; and the held chord is silent until the next key event | `arp.releaseVoice` closes the gate; `router.retakeVoice` re-asserts the current resolution immediately |
| T3 | All notes released while arp running, hold off | The pending note-off is skipped because "no note this step" → sustains forever | The gate-off is an **independent deadline**, not conditioned on a note existing |
| T4a | Hold engaged with keys down | — | Latched set := live set. Nothing audible changes |
| T4b | Hold engaged with no keys down | Could latch a chord from minutes ago | `numLatched` is zeroed whenever hold is off |
| T4c | Hold disengaged while keys down | — | Active set becomes the live set — same notes, seamless |
| T4d | Hold disengaged with no keys down | Latched chord plays forever — the classic latch stuck note | Active set empties; the pending gate-off still fires via T3's mechanism |
| T5 | Last note released while hold ON | — | The latched set is the arp's **own copy**, so the stack emptying does not disturb it. This is why the latch cannot live in `NoteStack` |
| T6 | New note pressed while hold ON | Ambiguous — a real behavioural fork | Section 9: **replace** |
| T7 | `releaseResources()` / device stop mid-step | Restart resurrects a gate or a latched chord | `arp.reset()` clears gate, latch, clock, walker. Ordered `voice.reset(); router.reset(); arp.reset();` |
| T8 | Sample-rate change | Stale `samplesUntilGateOff` in old-rate samples | `arp.prepare (sampleRate)` calls `reset()` |
| T9 | Env Destination = Filter, arp on | Not a bug, but note-offs produce no amplitude articulation and it reads as a broken gate | Documented, not coded — section 10 |
| T10 | Arp on with an empty set, hold off | A free-running clock makes the first note of a new chord land up to a step late | **Idle fast path** resets the clock, so the first note lands at the top of the block its key event was drained in. No host sync to stay aligned with, so nothing is lost |

**Honest limitation:** key events are still drained at *block* granularity (item
4's deliberate scope). So **steps are sample-accurate; the human's first key press
still quantises to a block boundary**. A few milliseconds, dwarfed by human
timing jitter — but written down rather than implied.

### What walking the table actually found (build step 6)

The table above was written before the code. Walking it against the running code
turned up **two defects the table had not anticipated**, which is the entire
argument for step 6 being a step rather than something trusted to Polish.

**T4b arrives through a second door — a latched chord outliving the arp being
switched off.** Hold is maintained by `resolveActiveNotes`, which only runs while
the arp is *on*. So the "`numLatched` is zeroed on every hold-off call" mechanism
that makes T4b safe is **frozen for as long as the arp is off**. Latch a chord,
switch the arp off, lift every key, switch the arp back on — and a phrase that
ended minutes ago starts playing with nothing held. Not a stuck note, but the
same class of surprise, and reachable in four moves.

Fix: `Arpeggiator::releaseVoice` clears the latch as well as the gate, the clock
and the walker. The latch is *phrase state*, exactly like `lastNoteNumber`, and
belongs with it — `releaseVoice` already ends the phrase, it just wasn't ending
all of it.

**A step can open a note on top of an already-open gate.** Section 3 lists "tempo
raised mid-gate" as case 2 of why the loop tracks two deadlines, but only as a
reason not to assume alternation — it never said what to *do* there.
`gateSamples` is fixed at note-on, so `setTempo`'s clamp can pull the next step
boundary in front of a pending gate-off. One knob drag reaches it: 60 BPM 1/4 →
300 BPM 1/32.

That step's `voice.noteOn` then arrives **gated**, taking `SynthVoice`'s overlap
branch instead of its fresh-trigger one — the pitch glides in rather than
snapping, and in Legato mode the envelope does not re-pluck at all. Section 10's
whole analysis ("every arp note-on arrives ungated") was *nearly* true rather
than true. One step articulated differently after a tempo jump is exactly the
"sounds slightly off rather than fails" bug this item exists to avoid.

Fix: close an open gate immediately before opening the new note. Deliberately
there rather than unconditionally at the boundary — when nothing is held there is
no note to open and T3 needs its independent deadline left alone, and a future
Tie mode opts out by skipping those four lines.

### The table is now a test, not a checklist

`runArpTransitionSelfTest` drives whole blocks through `renderVoiceBlock` — the
real hand-over, the real router, the real voice — and asserts **actual silence in
the rendered output**, the same criterion section 12 gives the human at the
speakers. T1, T2, T3, T5, T7, T8, T10, the latch defect above, and a
deterministic 250-round stuck-note fuzz. T4a/T4b/T4c/T4d and T6 stay asserted at
the `resolveActiveNotes` level in `runArpPatternSelfTest`, where they are cheaper
and sharper; T9 is documentation, not behaviour.

Two structural notes:

- **The hand-over moved out of `getNextAudioBlock` into a free
  `renderVoiceBlock`**, purely so the test drives the real thing. A test that
  re-implements the logic it is checking proves nothing, and the hand-over is
  precisely where a stuck note comes from. Ownership is unchanged: `MainComponent`
  still owns the voice, the router, the arp and `arpWasOn`, and passes them in.
- **The fuzz asserts it made a sound.** 154 of its 250 rounds are audible; a
  settle-to-silence assertion is trivially satisfied by a run that never played
  anything, so the count is checked against a loose floor.

> **Carried forward, unresolved:** `jassert` only breaks **when a debugger is
> attached** — JUCE's `jassertfalse` is guarded on that. Launched from Explorer
> or a plain shell, a failing self-test logs and carries on, and the app comes up
> looking fine. Both defects above were confirmed by temporarily replacing the
> assertions with a failure counter written to a file, because "it launched and
> stayed up" turned out to prove nothing. Section 12 lists "the self-tests pass"
> as objectively claimable; that is only true when the app is **run under the
> debugger (F5), not just built**. Worth deciding in step 7 or later whether the
> self-tests should report failures without a debugger — it affects all six, not
> just this one, so it is not a step-6 change to make silently.

---

## 8. Hold / latch

The latched set is the **arp's own fixed array**, sized to `NoteStack`'s cap:

```cpp
std::array<NoteStack::HeldNote, 16> latched {};
int numLatched = 0;
```

It must not live in `NoteStack`, because the whole point is that it survives the
stack going empty (T5) — and a latch inside the stack would mean note-offs
sometimes remove notes and sometimes do not, which is precisely the kind of
conditional state that produces stuck notes.

### The rule — REVISED after it was heard to be wrong

The first cut of this rule was **the latched set is the live held set whenever
the live set is non-empty, and frozen when it empties** — a straight copy, no
event flag. Caught immediately in play, exactly as this section originally
predicted it might be (see the old "what it gives up" note, kept below): a real
hand never releases every finger of a chord on the same sample. Holding C+E+G
and lifting fingers one at a time made the live set shrink `{C,E,G}` → `{C,E}` →
`{C}` → `{}`, and since a plain copy tracks the live set exactly, the latch
shrank on every one of those intermediate blocks and froze on whichever single
note happened to come up last — audibly, "only one note plays" after releasing
a chord.

**Fixed exactly as anticipated: a `latchAwaitingFreshChord` flag plus a union
instead of a copy.**

> *Hold remembers the last chord you were holding. Press anything new after
> releasing everything and it remembers that instead. Add a finger while still
> holding — or release one — and it keeps remembering the whole chord until the
> very last finger comes up.*

- **Live set goes empty** → arm `latchAwaitingFreshChord`. The latch itself is
  left untouched (T5).
- **Live set non-empty, flag armed** → a fresh phrase: **replace** the latch
  wholesale, clear the flag (T6).
- **Live set non-empty, flag not armed** → mid-phrase: **union** the live notes
  into the latch — add anything not already there, remove nothing. Every
  intermediate release is a *subset* of what got latched at the fullest point of
  the press, so the union adds nothing and (critically) removes nothing until
  the live set actually goes empty.

This still reproduces both classic hardware behaviours, now correctly across a
staggered release too:

- Press a key with everything else up → **replace** ✓
- Press a key while another is down → **add**, and releasing one of them
  afterwards no longer drops it from the latch ✓

Still depends only on the *stack plus one bit of phrase-boundary memory*, never
on watching the raw event stream mid-phrase — the arp samples the stack once
per block and genuinely **cannot see** an "everything released, then a new key
pressed" pair that happened inside a single block, so the flag is armed by
*emptiness*, not by any particular event.

**What it now gives up**, stated plainly: two chords played back-to-back with
fingers physically overlapping — never a fully empty live set between them —
union together rather than one replacing the other, since the flag never gets
armed. Taste, not a bug; overturn it by ear if it reads wrong in play.

---

## 9. `VoiceParameters` — six atomics, none smoothed

```cpp
enum class ArpPattern : int { Up = 0, Down, UpDown, Random, AsPlayed };

std::atomic<int>   arpEnabled     { 0 };                              // off on first load
std::atomic<int>   arpPattern     { (int) ArpPattern::Up };
std::atomic<int>   arpDivision    { (int) StepDivision::Sixteenth };
std::atomic<int>   arpHold        { 0 };
std::atomic<float> arpTempoBpm    { 120.0f };
std::atomic<float> arpGateLength  { 0.5f };
```

Validated against the three established conventions rather than assumed:

| Parameter | Convention | Test applied |
|---|---|---|
| `arpEnabled` / `arpPattern` / `arpDivision` / `arpHold` | Discrete, raw per block | Enums as `atomic<int>` — identical to `envelopeDestination`, `lfoWaveform`, `notePriorityMode` |
| `arpTempoBpm` | Time constant, raw per block | Directly assigned as an output and stepped per sample? No. Changing it affects only future rate? Yes. Same as `lfoRateHz` |
| `arpGateLength` | Raw per block | Not a rate, but likewise never assigned as an output — consumed at exactly one instant per step |

**Smoothing the tempo would be actively wrong, not merely wasteful**: it is
consumed once per *step*, not per sample, so a smoother would low-pass a value
nobody reads continuously and make the knob lag for no benefit. Gate is a
*fraction* rather than a time, so changing tempo does not also change
articulation.

These are strictly not *voice* parameters — the arp is a peer class — but they
share the one UI→audio channel and `MainComponent`'s spec tables are typed
against this struct. Splitting them out would buy purity and cost a second spec
table; item 6's real UI pass is the place to revisit that.

**The window must grow to `setSize (900, 660)`** in the same step that adds the
rows. 27 rows needs 420 px in the taller column and only 358 px exists —
[note-handling-design.md](note-handling-design.md) records this exact overflow
happening before. Not a Polish afterthought.

---

## 10. Interaction with existing features

### Glide and legato go inert while the arp runs — confirmed, and accepted

Arp steps go through `voice.noteOn`, **not around it** — it is the documented
reuse boundary, and an arp step genuinely *is* a press. (`retargetPitch` means "a
key came up revealing another still held", never true of an arp step.)

Now trace it. The sequence is `noteOn` … `noteOff` at gate … `noteOn`. Since
`noteOff()` sets `voiceGated = false`, **every** arp `noteOn` arrives ungated and
takes the `if (! voiceGated)` branch:

```cpp
glide.setTarget (pitchLog2Hz);
glide.snapToTarget();          // pitch SNAPS
envelope.noteOn();             // envelope ALWAYS retriggers
```

Two consequences, both deliberate and both worth telling the user rather than
leaving to be discovered:

1. **Glide Time has no audible effect at all while the arp is on.** Correct
   default — a 200 ms glide at 1/16 would smear the arpeggio into a siren — but
   it means a live-looking slider does nothing, which reads as a bug.
2. **Glide Mode (Legato/Retrigger) is likewise bypassed**, since the mode is only
   consulted on the `else` branch. Every step plucks in both modes. Also correct.

**Note Priority is also inert** while arping, since the arp reads the whole held
set rather than a resolution.

Three controls therefore do nothing while the arp runs. **Decision: accept and
document**, with a UI hint. Zero code.

### The escape hatch, if slurred arps are wanted later

A **Tie** gate mode — never close the gate, and call `noteOn` while still gated →
it takes the `else` branch → `glide.setTarget` without a snap (glide audible) and
Glide Mode decides re-plucking. Roughly ten lines, and **the two-deadline loop
already supports it** (`gateIsOpen` simply stays true across a boundary), which is
part of why section 3 declined to assume alternation.

Scoped as an explicit follow-up. The *real* slur is item 7's **per-step** slide
flag, which is where an acid bassline actually wants it — per step, not
all-or-nothing.

### Everything else

- **Velocity** — passed from the held set through to `voice.noteOn`. It goes
  nowhere in the voice yet, but plumbing it now costs nothing and is what item 7's
  accent and character-and-vim B5 will read.
- **LFO** — free-running and unaffected. LFO→pitch still applies per step, which
  is a genuinely nice sound.
- **Release time** — a long release relative to the gate gap makes steps overlap
  by tail. Normal and desirable; at 5 s release and 1/16 it becomes a wash, which
  is a choice rather than a bug.
- **Env Destination = Filter** — the VCA sits at unity, so note-offs produce no
  amplitude articulation and the arp reads as a filter wobble on a drone. Likely
  to be diagnosed as "my arp has no gaps"; it is T9.

---

## 11. Build order

| # | Name | Model | Work | Ends with |
|---|---|---|---|---|
| **0** | **Design** | Sonnet | This document + `architecture.md` / `TODO.md` links. **No code.** | Design captured before implementation |
| 1 | **Clock** | **Opus** | `StepClock.h` + `runStepClockSelfTest` wired into the existing Debug block | Clock maths provably correct in isolation. Silent — **the self-test is the deliverable**. Mirrors item 4's Fifo step |
| 2 | **Pattern** | **Opus** | `NoteStack` accessors; `NoiseGenerator::nextUInt32`; walker + `runArpPatternSelfTest`; extend `runNoteStackSelfTest` | Walker provably correct including every changing-set recipe. Still silent |
| 3 | **Render** | **Opus** | Sub-block loop; `VoiceDrive`; `releaseVoice`/`retakeVoice`; the `getNextAudioBlock` split; tempo + division controls; **window 660**. Pattern hardwired to Up | **First audible milestone.** Biggest step of the item |
| 4 | **Modes** | Sonnet | Arp Pattern combo + Arp Gate slider | All five patterns switchable live. Separate from 3 so "clock wrong" and "walker wrong" fail independently by ear |
| 5 | **Hold** | Sonnet | Latched set, `arpHold`, `resolveActiveNotes`, idle fast path | Release the keys and it keeps running |
| 6 | **Transitions** | **Opus** | Walk section 7's table against the running app; stuck-note fuzz | ✅ Done. Two defects found and fixed (section 7, "What walking the table actually found"); table is now `runArpTransitionSelfTest` |
| 7 | **Polish** | Sonnet | Comments; the `snapshotParameters` change-guard (section 13); reconcile this doc; tick TODO | ✅ Done. Change-guard implemented (section 13 point 1 resolved); comments reviewed, none stale; doc reconciled below. **Item 5 closed** |

Opus on **1, 2, 3, 6** — the four sub-steps where a wrong answer is silent: drift
maths, changing-set walking, loop termination/ordering, and stuck notes. Steps 4,
5 and 7 are combo rows, a short copy and a comment pass.

### How to run a step

```
Build Clock from documents/arpeggiator-design.md
```

---

## 12. Verification

### Objectively claimable

Builds clean Debug **and** Release, zero new warnings. Launches, no crash, no
stderr. **Six** Debug self-tests pass (the three existing plus
`runStepClockSelfTest`, `runArpPatternSelfTest` and `runArpTransitionSelfTest`) —
but see the caveat at the end of section 7: they only *report* a failure under a
debugger. With the arp off the audio path is unchanged from item 4.

`runStepClockSelfTest` covers: `samplesPerStep` exactness; **the anti-drift
assertion** (100 steps must sum to within 1 sample of `100 × 5512.5` — with `=`
instead of `+=` it would be off by 50); **triplet identity** (three 1/8T steps sum
exactly to one 1/4); an awkward block size (511 samples) over 10 seconds; and that
repeated `setTempo` with an unchanged value does not move the countdown.

`runArpPatternSelfTest` covers all five modes, **UpDown on two notes** (the case
that decides the endpoint rule), UpDown on one note (must terminate), and the
changing-set recipes — removal while sitting on the removed note, and addition
landing in pitch position.

### Needs a human at the speakers

- **It arpeggiates**: Arp On, 120 BPM, 1/16, Up, Gate 0.5, Env Destination Amp.
  Hold C4+E4+G4 → repeating C-E-G, four notes per beat.
- **Division is right**: 1/4 → one note every half-second against a phone
  metronome at 120. 1/8 → exactly double. **1/8T → three notes in the time two
  1/8s took.** The triplet check catches a wrong table entry with no instrument.
- **Tempo is a time constant**: at 60 BPM 1/4, drag Tempo to 200 → the current
  note finishes at roughly its old length, *then* it speeds up. No click, no
  double-trigger, no missed step. Direct analogue of item 4's glide-mid-ramp test.
- **Up-down on exactly two notes** → C-E-C-E, **not** C-C-E-E.
- **Changing set**: Up over C+E+G, release E → next cycle C-G with no repeat and
  no skip; press E again → rejoins **in pitch position**. The recipe an
  index-based walker fails.
- **As-played is actually different**: press G, then C, then E → expect G-C-E.
  The only mode that differs from Up on this input.
- **Random no-repeat** with exactly two notes → strict alternation.
- **Arp OFF with the key still down** → the held note sounds again **immediately**,
  not on the next key press. Missing `retakeVoice` fails exactly here.
- **Stuck-note fuzz**: 30 seconds mashing Arp and Hold on/off while pressing and
  releasing keys in random order. End with every key up and Arp off → **must be
  silent**. *Now also automated* as 250 deterministic rounds inside
  `runArpTransitionSelfTest`, which settles and asserts every round rather than
  once at the end. Still worth doing by hand once — real fingers reach timings a
  seeded generator does not — but it is no longer the only check.

### Timing accuracy without an oscilloscope

**The honest finding first: clock drift is not detectable by ear, and that is
exactly why it gets an assertion instead of a listening test.** A `=`-for-`+=`
bug at 130 BPM / 1/16 loses ~0.1 ms per second — about 12 ms after two minutes,
below the threshold at which it is distinguishable from a human's own timing.

- The real check is the **100-step sum assertion**, which catches a 50-sample
  error the ear would need an hour to notice.
- The tractable human check: record 60 s to WAV (Audacity, or OBS to file) at
  120 BPM / 1/16 / Gate 0.2 with a short attack, put the cursor on the first onset
  and on one ~60 s later, and read the sample delta. It should equal `N × 5512.5`
  to within a couple of samples. Two minutes of work, exact, and it validates the
  accumulator end-to-end through the real audio callback.
- A coarse check that *is* ear-doable: 60 BPM / 1/4 → exactly one note per second
  against a clock's second hand. Catches a division-table error or a
  factor-of-two, not drift.

### Taste — the user's call alone

Whether up-down should repeat endpoints; whether random should be allowed to
repeat; whether Gate 0.5 is the right default; whether these are the right seven
divisions; whether hold's **replace** rule feels right in play; whether arp+glide
doing nothing is acceptable or Tie mode is wanted; whether it *feels* like an
SH-101 arp.

### Unverifiable — flag, do not claim

Sub-sample step accuracy by ear. MIDI-driven arping (the same no-hardware gap
item 4 already carries — the arp reads the same `NoteStack` all three sources
feed, so there is no arp-specific MIDI risk, but it stays untested end-to-end).
Whether it feels in time on the Pixel. CPU cost of the sub-block split at small
buffer sizes on the Pixel.

---

## 13. JUCE APIs — flagged, not assumed

**Confident**, all already used successfully here: `jmin`/`jmax`/`jlimit`,
`jassert`, `ScopedNoDenormals`, `AudioBuffer::getWritePointer`/`copyFrom`,
`ComboBox` and `Slider` wiring, `setSize`, `prepareToPlay`/`releaseResources`.

**Resolved in step 7 (Polish):**

1. **`juce::SmoothedValue::setTargetValue` called repeatedly mid-ramp with an
   unchanged target — does it restart the ramp?** This became live because
   `snapshotParameters` now runs 2–3× per block instead of once, whenever the
   arp lands more than one step in a block. Rather than spend a step
   confirming JUCE's behaviour, the mitigation that is correct either way was
   implemented: `SynthVoice` caches the last-stored value of each of the
   twelve smoothed parameters and `snapshotParameters` only calls
   `setTargetValue` when the atomic has actually moved since the last call.
   Costs one float comparison per parameter per call; the jump path
   (`prepare()`, once) is unaffected and refreshes every cache regardless. The
   underlying JUCE question is now moot rather than answered.

**Needs confirmation — designed so it does not matter:**

2. **Three-argument `juce::jmin`** — used in the chunk calculation. Believed to
   exist; trivial fallback is nested two-argument calls.
3. **`std::span`** — C++20 standard library, not JUCE. See section 5 for the
   fallback.

Deliberately avoided so as to need no confirmation at all: `std::lround`
(replaced by `(int)(d + 0.5)`, which raises no rounding-mode question) and
`juce::Random` (replaced by the project's own `NoiseGenerator`).

---

## 14. Out of scope

Octave range (deferred by choice); swing and timing/velocity humanisation
([character-and-vim.md](character-and-vim.md) B2 — but `getStepIndex()` exists so
B2 has its parity source ready); a Tie / gate-100% mode (section 10, scoped as a
follow-up); host sync (forbidden by CLAUDE.md); the step sequencer and its pattern
storage (item 7 — `StepClock` is what it inherits); accent and slide (item 7);
sample-accurate *key* input (item 4's deliberate block granularity, unchanged);
polyphony (forbidden).
