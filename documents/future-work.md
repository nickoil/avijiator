# Future work — not scoped into any numbered TODO item

Two ideas raised in discussion, both bigger than a checklist line and both
requiring an explicit decision before design work starts. Neither is queued;
this document exists so the reasoning behind them isn't lost, and so a future
session can pick one up with `Build <thing> from documents/future-work.md`
once it's actually decided on, the same way the numbered items point at their
own design docs.

---

## 1. VST3 instrument conversion (for host tempo sync)

**The blunt framing first:** wanting the arp/clock in sync with other
instruments is not an incremental add on top of the standalone app — it is the
direct reversal of a constraint [CLAUDE.md](../CLAUDE.md) currently states as
deliberate:

> **No host sync.** This is a standalone app — internal tempo only, no DAW clock.

...and [architecture.md](architecture.md) frames the whole project the same
way ("compiled as a **standalone app** — no DAW, no VST host needed on
stage"). Confirmed by grep: `AudioPlayHead`, `getPlayHead`,
`CurrentPositionInfo`, `PositionInfo` appear **nowhere** in `Source/` — only
inside the JUCE library itself. There is no host-tempo plumbing to extend;
all of it would be new.

### What actually has to change

This touches the shape of the whole app, not one file:

| Area | Today | For a VST3 instrument |
|---|---|---|
| CMake target | `juce_add_gui_app(...)` (`CMakeLists.txt`), explicitly commented "no VST/AU wrapper" | `juce_add_plugin(... FORMATS VST3 IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE ...)` + link `juce_audio_processors` |
| Entry point | `Source/Main.cpp` — `JUCEApplication` + `DocumentWindow` | Gone; JUCE's plugin wrapper supplies it. `Main.cpp` deleted, or kept as a second `Standalone` format target alongside `VST3` |
| Top-level class | `MainComponent : juce::AudioAppComponent, juce::MidiInputCallback` | Split into an `AudioProcessor` (DSP/state) and an `AudioProcessorEditor` (UI, mostly portable as-is) |
| Audio callback | `getNextAudioBlock (AudioSourceChannelInfo&)` | `processBlock (AudioBuffer<float>&, MidiBuffer&)` — different signature; internals (arp hand-over, denormal guard, `voice.renderNextBlock`) port with modest rework |
| MIDI input | `MidiInputCallback` on a separate MIDI thread + device enumeration (`enableAllMidiInputs`, `handleIncomingMidiMessage`) | Host hands MIDI over pre-merged, sample-accurate, inside `processBlock`'s `MidiBuffer` — no device thread at all. `NoteRouter`'s two-FIFO design (built specifically because MIDI and UI are *different threads*) is worth re-examining once host MIDI arrives on the audio thread itself |
| Host tempo | Not read anywhere | `getPlayHead()->getPosition().getBpm()` feeding `StepClock::setTempo`, replacing or supplementing today's UI-slider-only path |
| Parameters | Raw `std::atomic` fields on `VoiceParameters`, driven by debug sliders via `onValueChange`/`onChange` lambdas | `AudioProcessorValueTreeState`, so the host can automate/save/restore each one |
| Device settings UI | `showAudioSettings()` / `AudioDeviceSelectorComponent` | Removed — host owns routing entirely |

### What survives mostly untouched

The DSP core (`SynthVoice`, `Vcf`, `Adsr`, `Lfo`, the oscillators),
`NoteRouter`'s stack/priority logic, and `Arpeggiator`/`StepClock`'s
drift-free step math don't know or care whether they're driven by a
standalone app or a plugin wrapper. The rework concentrates entirely in the
boundary layer: entry point, callback shape, MIDI intake, UI hosting,
parameter exposure.

### Recommendation

Not TODO-item-sized — closer in scope to a new Stage, on the order of what
Stage B (Android) already is. It also forces a real design decision
CLAUDE.md currently forecloses: does host sync *replace* the internal clock
outright, or become an optional toggle, keeping the standalone identity
available for unplugged stage use? That decision, and the MIDI-timing model
change in particular (host `MidiBuffer` timing is not the same shape as the
current MIDI-thread callback), are exactly the kind of thing that goes wrong
*quietly* rather than loudly — this deserves its own design doc, written
**before** any code, the same discipline `arpeggiator-design.md` got.

---

## 2. A drum voice

### Reusable today, zero new DSP components needed

- **`NoiseGenerator`** (`Source/DSP/NoiseGenerator.h`) — a hand-rolled,
  seedable xorshift generator already instantiated three times independently
  (audible noise, filter floor noise, LFO sample-and-hold). Most of a
  snare/hihat/clap's raw material, for free.
- **`Vcf`** (`Source/DSP/Vcf.h`) — the two-stage SVF cascade, already
  self-oscillating and tunable, is exactly what tone-shapes noise into
  "snare" vs "hat" vs "clap" character.
- **The shared `Adsr`** (`Source/DSP/Adsr.h`) — defaults are already short and
  percussive (10ms attack, 100ms decay), since that's what the SH-101 voice
  itself wants. No "fast envelope" capability needs building.
- **The unused `currentVelocity` hook** (`Source/DSP/SynthVoice.h`) — captured
  on every `noteOn` but not routed anywhere yet; a natural accent input for a
  drum voice, already sitting there.

### The one missing piece

A classic kick-drum pitch-drop — a fast downward pitch sweep on trigger,
independent of note history. Neither existing modulation source is quite it:
`Glide` is note-to-note portamento (it only engages between two held notes,
and always snaps on a fresh trigger from silence), and the LFO is periodic,
not one-shot.

The cheapest fix does **not** mean inventing a new component: add **Pitch**
as a fourth `EnvelopeDestination` alongside `Filter`/`Amp`/`Both`
(`Source/DSP/VoiceParameters.h`), so the existing shared ADSR's decay stage
modulates pitch the same way it already modulates cutoff. That reuses the
architecture's own "one shared envelope, routable" idea instead of fighting
it with a second envelope.

### What's off the table — stated as deliberate in CLAUDE.md, not gaps

- **"Monophonic. One voice. Don't add polyphony."** A drum *kit* where a kick
  rings under a hi-hat hit needs two things sounding at once. The current
  one-voice architecture can't do that without an explicit, separate decision
  to relax this constraint.
- **"One shared ADSR... don't 'helpfully' add a second one."** A drum voice
  wanting independent pitch/amp/filter envelopes (standard on real drum
  synths) can't get that for free; it has to keep sharing the one envelope,
  as above.

### Recommendation

**The easy version:** a single percussion sound built entirely from existing
parts — noise level up, the SVF tuned per drum character, the existing shared
ADSR, optionally the new Pitch destination for a kick-style drop — played
through the *same* one mono voice, one hit at a time, the same way the
arpeggiator already borrows the voice rather than owning a second one. That's
squarely inside the current architecture and genuinely cheap; if this gets
picked up it's Sonnet-level UI/parameter work plus one small, well-scoped DSP
addition (the Pitch destination), not a new item 2-sized undertaking.

**The hard version — a real drum kit** with multiple elements sounding
together is blocked by the monophonic constraint as it stands today. That's
not "a kind of drum synth" to pick, it's a fork in the project's core
architecture and needs its own explicit sign-off before any design work.
