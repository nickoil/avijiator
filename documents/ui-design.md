# UI design

Design record for **Stage A item 6** ([TODO.md](TODO.md)). Written before
implementation, same discipline as [dsp-voice-design.md](dsp-voice-design.md),
[envelope-lfo-design.md](envelope-lfo-design.md),
[note-handling-design.md](note-handling-design.md) and
[arpeggiator-design.md](arpeggiator-design.md).

Items 2-5 are complete, and every one of them shipped its controls as
*explicitly throwaway* scaffolding — 19 `LinearHorizontal` sliders and 8 combo
boxes in two hand-computed columns, no `LookAndFeel`, fixed 900x660 window.
`MainComponent.h` says so in as many words. Item 6 is where that gets replaced
by a real instrument panel.

**Nothing in the signal path changes.** This item adds zero code to
`getNextAudioBlock`, touches no DSP class, and reuses `VoiceParameters`
verbatim. It is a presentation-layer item only. If a step in the build order
below finds itself editing `SynthVoice`, `Vcf`, `Adsr`, `Lfo` or `Arpeggiator`,
something has gone wrong — stop and re-read this line.

> **Model note**: item 6 is not on [CLAUDE.md](../CLAUDE.md)'s Opus list (only
> items 2 and 5 are named). Every step below is **Sonnet** work — layout,
> `LookAndFeel` drawing, and widget plumbing, with no silent-failure DSP
> anywhere in it. Lower `/effort` is appropriate for steps 4-7.

---

## 1. Decisions and why

Settled with the user before writing this doc. Recorded here so a later session
does not reopen them.

| Decision | Choice | Reasoning |
|---|---|---|
| Mockup first? | **Yes** — Claude Design canvas, then hand-code JUCE against it | [TODO.md](TODO.md) item 6 nominates it. Layout mistakes get found before 400 lines of C++ exist. Visual reference only: **no code transfers**, every `Slider`/`LookAndFeel` is still hand-written |
| Visual style | **Clean modern flat** — dark, not skeuomorphic grey hardware | Less drawing code than a photoreal panel, and reads better small. `architecture.md`'s "familiar layout" is about *arrangement*, which is honoured; it does not require a fake aluminium texture |
| Scaling | **Fixed design canvas + global `AffineTransform` scale** | One layout, authored once, scaled to fit whatever the window or screen is. The only approach that ports to Android without designing every breakpoint twice |
| Item 7 sequencer | **Reserve an empty region now** | Costs nothing today and saves a layout redesign when item 7 lands |
| Arp params stay in `VoiceParameters` | **Yes, leave them** | [VoiceParameters.h](../Source/DSP/VoiceParameters.h) flags item 6 as the place to revisit this. The answer is no: splitting buys purity and costs a second spec table for zero user-visible gain. That comment gets updated to record the decision, not deleted |

---

## 2. Control inventory

All 27 live parameters, regrouped into panel sections. **The counts are
load-bearing** — 19 float controls and 8 discrete controls must still be
accounted for after the move, or a parameter has been silently dropped.

| Section | Cells | Controls |
|---|---|---|
| **VCO** | 5 | Saw, Pulse, Pulse Width, Sub, Noise |
| **VCF** | 3 | Cutoff, Resonance, Env→Cutoff |
| **ENV** | 5 | Attack, Decay, Sustain, Release, Destination *(choice)* |
| **LFO** | 4 | Rate, Waveform *(choice)*, →Pitch, →Cutoff |
| **KEYBOARD** | 3 | Glide Time, Glide Mode *(choice)*, Note Priority *(choice)* |
| **ARP** | 5 | On + Hold *(two toggles, one stacked cell)*, Pattern *(choice)*, Division *(choice)*, Tempo, Gate |
| **OUTPUT** | 1 | Level |
| **SEQ** | — | *reserved, empty — item 7* |

Totals: 19 knobs, 6 combo boxes, 2 toggles = 27. ✓

Two widget changes, **no wiring changes**: `Arp On` and `Arp Hold` become
`ToggleButton`s instead of two-item combo boxes. They still store `0`/`1` into
the same `std::atomic<int>`, so nothing downstream notices.

### Ranges — carried over unchanged

Copied from the existing `debugControlSpecs` table. These were tuned by ear
during items 2-5 and there is no reason to re-derive them; the *presentation*
is what changes, not the values.

| Control | Min | Max | Default | Notes |
|---|---|---|---|---|
| Saw / Pulse / Sub / Noise | 0.0 | 1.0 | 0.70 / 0 / 0 / 0 | |
| Pulse Width | 0.02 | 0.98 | 0.50 | |
| Cutoff | 20 | 18000 | 2000 | **stored as log2**, skewed from midpoint |
| Resonance | 0.0 | 1.0 | 0.20 | |
| Level | 0.0 | 1.0 | 0.25 | |
| Attack / Decay / Release | 0.001 | 5.0 | 0.01 / 0.1 / 0.3 | seconds |
| Sustain | 0.0 | 1.0 | 0.7 | |
| Env→Cutoff | 0.0 | 8.0 | 0.0 | octaves |
| LFO Rate | 0.02 | 20.0 | 2.0 | Hz |
| LFO→Pitch | 0.0 | 1.0 | 0.0 | octaves |
| LFO→Cutoff | 0.0 | 8.0 | 0.0 | octaves |
| Glide Time | 0.0 | 5.0 | 0.0 | seconds per octave |
| Arp Tempo | 20 | 300 | 120 | BPM |
| Arp Gate | 0.05 | 0.95 | 0.50 | fraction of step |

The time-valued knobs (Attack/Decay/Release, Glide) should get a skew so the
short end is not crammed into the first few pixels — the same treatment Cutoff
already has, applied for the same reason. Cutoff keeps `storeAsLog2 = true`;
nothing else does.

---

## 3. The design canvas — and where its numbers come from

**Canvas: 1280 x 660. Knob diameter: 76px.**

Neither number is arbitrary, and the derivation matters because it is the one
place this item makes a decision that Stage B has to live with.

### Knob diameter is set by the Android touch target

The invariant that survives scaling is the **ratio** of knob diameter to canvas
width, not the pixel count. Working backwards from a Pixel 8 in landscape
(2400 x 1080 physical, density ~2.625, so 914 x 411 **dp**):

```
scale to fit  = min(2400/1280, 1080/660) = min(1.875, 1.636) = 1.636
knob physical = 76 x 1.636 = 124 px
knob in dp    = 124 / 2.625 = 47.4 dp
```

That lands on the ~48dp minimum comfortable touch target. A 60px knob would
give 37dp and be too small; 76 is the smallest round number that clears the
bar. **This is why the knobs look oversized on a desktop monitor** — it is
deliberate, not a mistake to tidy up later.

### Canvas aspect is a compromise, recorded as such

1280 x 660 is 1.94:1. A Pixel in landscape is 2.22:1, so the scaled panel
leaves ~10% of the screen width unused (2094 of 2400). Matching the phone
exactly would mean a 1460 x 660 canvas, which is an awkwardly letterboxy shape
for a desktop window.

1.94 is the compromise. **Step 1's mockup is where this is confirmed or
changed** — it is much cheaper to discover the panel wants to be wider in an
HTML artboard than after the JUCE layout exists. If it changes, only the two
constants move.

### Vertical budget

411dp of height on a phone is genuinely tight, and it forces the layout to be
**wide and short**: sections sit side by side in a row of knobs, never stacked
into multi-row grids.

| Band | Height |
|---|---|
| top margin | 16 |
| header / status bar | 36 |
| gap | 12 |
| **section row A** — VCO \| VCF \| ENV (13 cells) | 158 |
| gap | 12 |
| **section row B** — LFO \| KEYBOARD \| ARP \| OUTPUT (13 cells) | 158 |
| gap | 12 |
| **SEQ reserved strip** | 90 |
| gap | 12 |
| on-screen keyboard | 80 |
| gap | 10 |
| button row (note buttons, Audio Settings) | 34 |
| bottom margin | 16 |
| **total** | **646** of 660 |

Horizontally: 1280 less 2x20 margins = 1240 usable. A cell is **88px** wide
(76px knob plus 6px either side). 13 cells x 88 = 1144, plus three 16px
inter-section gaps = 1192. Fits with ~48px of slack per row.

A cell is **116px** tall: 16 label + 4 gap + 76 knob + 4 gap + 16 value
readout.

---

## 4. Colour tokens

One accent plus one alternate. Section headers do *not* each get their own
colour — that reads as a toy, and the grouping is already carried by the
section fill and border.

| Token | Hex | Used for |
|---|---|---|
| `background` | `#14161A` | window behind the panel, including the letterbox bars |
| `panel` | `#1C2026` | the panel ground |
| `sectionFill` | `#22272E` | section backgrounds |
| `outline` | `#2E353F` | section borders, combo box borders |
| `knobTrack` | `#333B45` | unfilled arc |
| `accent` | `#4EC9C0` | filled arc, step-sequencer gate-on cells, toggle-on |
| `accentAlt` | `#E0A458` | knob pointer, ARP section active state, latched note buttons |
| `text` | `#E6EAF0` | control labels, values |
| `textDim` | `#8A94A6` | section headers, units, the reserved SEQ placeholder |
| `keyWhite` | `#C9D1DC` | on-screen keyboard naturals |
| `keyBlack` | `#2A313A` | on-screen keyboard sharps |

These are set **once**, via `setColour` on the `PanelLookAndFeel` instance,
against JUCE's ColourIds — `Slider::rotarySliderFillColourId`,
`Slider::rotarySliderOutlineColourId`, `ComboBox::backgroundColourId`,
`ComboBox::outlineColourId`, `Label::textColourId`,
`TextButton::buttonColourId`, `ToggleButton::tickColourId`,
`ResizableWindow::backgroundColourId`. Section components must never hard-code
a colour literal; they look it up. That way a palette change is one edit.

---

## 5. Structure

```
Source/UI/
  PanelLookAndFeel.h/.cpp   flat drawing + the palette
  ParameterControls.h       specs + the three attach() helpers + MomentaryButton
  PanelSection.h/.cpp       one titled group, lays its own cells out
  SynthPanel.h/.cpp         composes sections at the FIXED design size
```

`MainComponent` keeps the audio callbacks, the MIDI callback,
`keyStateChanged`, `focusLost`, `showAudioSettings`, the six Debug self-tests
and the `arpWasOn` edge detector. It loses every widget member and both spec
tables.

### The spec-table pattern is kept, not replaced

The existing pointer-to-member-atomic wiring is already exactly what
[CLAUDE.md](../CLAUDE.md) mandates, and it is the reason items 3, 4 and 5 could
each add controls by adding a table row rather than more copy-paste. It carries
forward, widened from two spec kinds to three:

```cpp
struct KnobSpec   { const char* name; double min, max, def;
                    bool storeAsLog2; double skewMidpoint;   // 0 = no skew
                    const char* suffix;
                    std::atomic<float> VoiceParameters::* target; };

struct ChoiceSpec { const char* name; const char* const* choices; int numChoices;
                    int defaultIndex;
                    std::atomic<int>   VoiceParameters::* target; };

struct ToggleSpec { const char* name; int defaultValue;
                    std::atomic<int>   VoiceParameters::* target; };
```

with `attachKnob` / `attachChoice` / `attachToggle` free functions holding the
`onValueChange` lambda **and the seed call** — the existing code's trick of
invoking the callback once at construction so the widgets and the atomics
cannot disagree at startup. That seed is easy to forget and its absence is
silent; putting it inside the attach helper means it cannot be.

`MomentaryButton` moves from `MainComponent.h` into `ParameterControls.h`
unchanged. It is already reused by the note buttons and the on-screen keyboard
and has no reason to stay private.

### SynthPanel's interface

Takes `VoiceParameters&` and a `std::function<void (const NoteEvent&)>` — fed
by `router.pushUiEvent` — so it never reaches into `SynthVoice` or `NoteRouter`
directly. `SynthPanel::resized()` lays out against the **design size only** and
never sees the real window size.

### The scaling, in `MainComponent::resized()`

```cpp
const auto scale = juce::jmin ((float) getWidth()  / (float) designWidth,
                               (float) getHeight() / (float) designHeight);

panel.setTransform (juce::AffineTransform::scale (scale));
panel.setBounds (0, 0, designWidth, designHeight);  // PRE-transform bounds
```

then centre the scaled result. The transform goes on the **child panel**, not
on `MainComponent` itself — JUCE routes hit-testing through a child's
transform, so mouse coordinates stay correct; a transform on the top-level
component is the version that misplaces clicks.

### CMakeLists.txt

Sources are listed explicitly — there is no glob. Every new `.cpp` must be
added to `target_sources` or it silently is not compiled:

```cmake
Source/UI/PanelLookAndFeel.cpp
Source/UI/PanelSection.cpp
Source/UI/SynthPanel.cpp
```

---

## 6. Three ways this fails silently

Worth their own section because none of them produces an error message.

1. **Keyboard focus.** Today every mouse-driven control calls
   `setWantsKeyboardFocus (false)`, so that clicking a slider does not steal
   focus and kill QWERTY note input. Combo boxes are deliberately left alone —
   they genuinely need keys to operate. **Every new widget must do the same**,
   or the computer keyboard stops playing notes after the first knob touch,
   with nothing logged and no crash. Put it in the attach helpers so it is not
   a thing anyone has to remember.

2. **A dropped parameter.** 27 controls go in, 27 must come out. A knob that
   never gets its `attach` call looks completely normal — it turns, it has a
   label, and it does nothing. Check the count against section 2's table at the
   end of step 5, and consider a `static_assert` on the table sizes, matching
   the one `MainComponent` already uses for `numDebugControls`.

3. **The self-tests.** The six `jassert` self-tests in `MainComponent`'s
   constructor must survive the rewrite intact. They only halt under a
   debugger, so "it launched and stayed up" is **not** evidence they passed —
   this is already recorded as a caveat on items 4 and 5 and it applies here
   too.

---

## 7. Build order

Steps 0-1 are one session; 2-4 a second; 5-7 a third. `/compact` within a
session, `/clear` between. A fresh session needs only "build step N from
documents/ui-design.md".

| Step | What | Done when |
|---|---|---|
| **0** | This document | — |
| **1** | Claude Design canvas mockup — desktop artboard at 1280x660, plus a phone-landscape artboard to test whether one layout scales or the aspect forces a variant | The grouping and knob sizing look right, and section 3's canvas numbers are either confirmed or amended here |
| **2** | `PanelLookAndFeel` — `drawRotarySlider`, `drawLinearSlider`, `drawComboBox`, `drawToggleButton`, `drawButtonBackground`, label/combo fonts. Flat fills, no gradients or bevels | Builds; a throwaway test knob draws with the new look |
| **3** | `ParameterControls.h` — the three spec structs and the three attach helpers | Builds; one section's worth of controls wired through it |
| **4** | `PanelSection` — titled group, lays its cells out with `juce::Grid` | Builds; VCO renders correctly standalone |
| **5** | `SynthPanel` — all seven sections, keyboard row, note buttons, Audio Settings button, empty SEQ strip, at the fixed design size | All 27 controls present and wired; count checked against section 2 |
| **6** | `MainComponent` integration + the scale transform; window becomes resizable | Panel scales with the window and the mouse still hits the right knob |
| **7** | Delete the scaffolding comments, tick item 6 in TODO.md, record the settled UI decisions in architecture.md | — |

**`setLookAndFeel` has a destructor obligation**: the panel must call
`setLookAndFeel (nullptr)` before the `PanelLookAndFeel` instance dies, or JUCE
asserts on shutdown. Easy to hit in step 6, trivial to fix, worth knowing in
advance.

---

## 8. Verification

### Claimable without ears

- `cmake --preset windows-x64`, then `cmake --build --preset debug` **and**
  `release` — zero warnings, matching items 2-5.
- Launch **under the debugger (F5)** so the six self-tests actually halt.
- All 27 controls present, and each one moves its parameter: sweep Cutoff and
  Resonance, switch Env Destination, toggle Arp on and change Division.
- **The focus trap specifically**: click a knob, then play Z/S/X — notes must
  still sound.
- Resize the window from small to large: the layout scales, nothing clips, and
  knobs still respond at the correct position. That last part is the real
  proof the transform is on the right component.
- The reserved SEQ region is visible and empty.

### Needs a human at the screen

Not claimable, flag them instead:

- Whether the panel **looks** right, and whether the grouping is the one that
  makes sense to play from.
- Whether the knobs are **big enough to hit on a phone**. Section 3 derives
  76px from a 48dp target, but that is arithmetic, not a touch test — and there
  is no touch hardware in Stage A. Real answer comes at item 9.
- Whether the arrangement reads as being in the SH-101 family.

---

## 9. Out of scope

Deliberately not in item 6, so they do not creep in:

- **The step sequencer** (item 7). The SEQ strip is reserved and empty. No grid
  widget, not even a disabled one.
- **Preset save/load.** Nothing is persisted. Note that the separate
  housekeeping item — remembering audio/MIDI device settings across restarts —
  is also independent of this and stays independent.
- **Any DSP change.** See the note at the top.
- **Touch input handling.** Stage B item 9. This item only makes touch-sized
  *decisions*; it does not add gesture code.
- **The character/"Vim" controls** (item 10). Those parameters do not exist yet;
  when they do they will add sections, which this structure is designed to
  absorb.
