# Avijiator — working agreement

Monophonic SH-101/TB-303-style synth in JUCE (C++). Windows standalone first,
Android port second.

**Read on demand, not by default**: [documents/architecture.md](documents/architecture.md)
(design rationale), [documents/TODO.md](documents/TODO.md) (checklist + current
state). Don't load either unless the task needs it.

## Session discipline

This project runs on a Claude Pro plan. Limits are drawn down by **context size ×
number of turns**, and the whole conversation is resent every turn — so a large
context costs allowance on every message, not just the one that created it.

- **One TODO item per session.** Finish it, then the user runs `/clear`. Don't
  carry the oscillator work into the filter session. **Claude must say so
  explicitly the moment an item's last step is done** — don't wait to be asked,
  and don't roll straight into the next item's planning. Multi-step items may
  span several steps in one session; `/compact` within an item, `/clear`
  between them. The step-0 design doc in `documents/` is what makes clearing
  cheap: a fresh session needs only "Build <step> from documents/<doc>.md".
- **Never read JUCE library sources.** They're enormous. If you need JUCE API
  behaviour, say what you need and ask — don't go spelunking in the headers.
- **Never dump raw build output.** MSVC template errors from JUCE run to
  thousands of lines. Report the first real error and the file:line, not the log.
- **Read narrowly.** Specific files and line ranges. Don't scan the tree to
  answer a question that names a file.
- **Plan mode before DSP work** (items 2 and 5). Re-work after 300 lines of DSP
  is the expensive failure mode here.

### Model policy

- **Sonnet** by default — CMake, JUCE boilerplate, UI layout, Gradle/Android
  plumbing, docs.
- **Opus** for TODO items 2 (oscillator + filter) and 5 (arpeggiator clock)
  only. These are where a wrong answer is *silent* — it sounds slightly off
  rather than failing a test. Opus draws on a tighter sub-limit, so don't spend
  it on boilerplate.
- Lower `/effort` for config and layout work; keep it high for DSP.

## Build and run

<!-- Fill in after TODO item 0 is done, then keep current. Cheaper to read one
     correct command here than to rediscover it every session. -->

CMake lives inside the Build Tools install, not on `PATH`:
`"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"`

- Configure: `cmake --preset windows-x64`
- Build: `cmake --build --preset debug` (or `release`)
- Run: `./build/Avijiator_artefacts/Debug/Avijiator.exe`
- **Always use the presets.** `CMakePresets.json` sets `toolset host=x64`, and
  that is load-bearing: the 32-bit hosted `cl.exe` runs out of heap space
  compiling JUCE in parallel (`error C1060`). `-A x64` alone does *not* fix
  it — that sets the target architecture, not the compiler's own.
- Toolchain: VS Code + CMake Tools, MSVC Build Tools (no Visual Studio IDE, no
  Projucer during Stage A). Static MSVC runtime (`/MT`) via
  `MSVC_RUNTIME_LIBRARY` — single-exe distribution, no redistributable.
- `JUCE_ASIO=1` is set (Windows only) for low-latency audio. JUCE 9 bundles
  the Steinberg headers, so no SDK download — but they are GPLv3-or-signed-
  agreement, which matters only if a build is ever distributed.

## Hard constraints

**Audio thread** (`getNextAudioBlock` / `processBlock`) — violating these
produces dropouts and clicks that are painful to diagnose after the fact:

- No allocation, no `delete`, no container resize, no locks, no file I/O, no
  logging, no `juce::String` construction.
- UI → audio parameter changes go through atomics or a lock-free FIFO, never a
  shared mutable field.
- Smooth cutoff/resonance/gain changes, or you get zipper noise.
- `juce::ScopedNoDenormals` at the top of the callback.

**DSP and architecture** — these are deliberate design choices, not gaps to fix:

- **Monophonic.** One voice. Don't add polyphony.
- **One shared ADSR**, routable to VCF/VCA/both. This mirrors the real SH-101's
  shared-envelope quirk. It is intentional. Don't "helpfully" add a second one.
- **PolyBLEP** oscillators, never naive saw/square — aliasing is the whole point.
- **SVF** filter first, not a ladder. Ladder is a later revisit, not a default.
- **Sample-accurate step timing.** Schedule note-on/off against exact sample
  positions within the audio block. Never `juce::Timer` for musical timing.
- **No host sync.** This is a standalone app — internal tempo only, no DAW clock.

**Stage B only**: `CMakeLists.txt` and the `.jucer` file are two separate project
definitions that must be manually kept in sync. Any file or module added to one
must be added to the other, in the same commit.

## What you cannot verify

Do not claim these are done or correct — flag them for the user instead:

- Whether the filter or oscillator **sounds** right, or is "in the family" with
  an SH-101. That's an A/B listening test the user does.
- Round-trip **latency** on the Pixel. Measured on hardware, not inferred.
- Whether the USB power chain (phone + hub + interface + MIDI) holds up.

"Builds and runs" is a claim you can make. "Sounds correct" is not.

## Open decisions — do not settle these silently

Marked open in architecture.md; the user decides by ear or by testing:

- Filter topology: SVF cascade vs. ladder emulation (start SVF).
- Note priority: last-note vs. highest-note.
- Real cost of the Windows → Android port.

If code needs one of these to proceed, pick the stated starting point, say
clearly that you did, and leave it easy to swap.

## Keeping docs current

When a TODO item completes, tick it in [documents/TODO.md](documents/TODO.md).
When an open decision gets settled, move it out of the open list in
[documents/architecture.md](documents/architecture.md) and record what was
chosen and why. Keep this file under ~150 lines.

# Compact instructions

Preserve: the current TODO item and its acceptance criteria, DSP decisions made
this session and the reasoning, and any unresolved build errors with their
file:line. Drop: build logs, file contents already written to disk, and
superseded approaches.
