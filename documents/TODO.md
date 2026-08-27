# TODO — SH-101-a-like

Tracks actionable work against the plan in [architecture.md](architecture.md).
Tooling decision: **VS Code + CMake** for the Windows dev stage (no Projucer,
no Visual Studio IDE); **Projucer → Gradle**, built/run from VS Code's
terminal, for the Android port stage (Android Studio kept only as a debugger
fallback, not the daily driver).

See also [future-work.md](future-work.md) for larger ideas that aren't scoped
into a numbered item yet (VST3 conversion, a drum voice).

## 0. Tooling setup

- [x] Install VS Code extensions: C/C++ (ms-vscode.cpptools), CMake Tools
- [x] Install Visual Studio Build Tools — "Desktop development with C++"
      workload only (headless compiler/linker/MSVC debugger engine, not the
      Visual Studio IDE)
- [x] Get JUCE source (submodule or CMake `FetchContent`) — used a git
      submodule pinned to tag `9.0.1` at `libs/JUCE`
- [x] Write root `CMakeLists.txt`: `juce_add_gui_app` standalone target,
      link required JUCE modules (audio basics, DSP, GUI) — configure-only
      check passes (VS 17 2022 generator); build/run/sound is Stage A item 1
- [x] Set static MSVC runtime linking (`/MT` instead of default `/MD`) via
      `MSVC_RUNTIME_LIBRARY` target property — needed for true single-exe
      distribution with no Visual C++ Redistributable dependency on the
      target machine — verified in generated .vcxproj: MultiThreaded(Debug)
      for Debug, MultiThreaded (/MT) for Release/RelWithDebInfo/MinSizeRel
- [x] Confirm CMake Tools can configure, build, run, and **debug**
      (breakpoint + step) a trivial JUCE app before writing real DSP

## 1. Stage A — Windows (dev)

Build/validate everything here before touching Android.

- [x] **1. Environment check** — minimal JUCE Windows standalone app builds,
      runs, makes sound — builds clean (Debug, VS 17 2022 generator), launches
      and stays up with no crash/stderr output; MainComponent plays a fixed
      440Hz test tone via AudioAppComponent. **Audible confirmation is yours
      to make** — I can't hear it.
- [x] **2. Oscillator + filter core** — saw/square + sub + noise mix →
      resonant lowpass; A/B against reference SH-101 recordings.
      Design + build order: [dsp-voice-design.md](dsp-voice-design.md).
      Builds clean (Debug + Release, zero warnings), all four sources +
      resonant filter working, self-oscillation confirmed, and a NaN
      stability bug found and fixed along the way (softClip on the feedback
      path — see section 3). **The actual acceptance criterion — "in the
      family" with real SH-101 recordings — is a listening test only you can
      do, and hasn't happened yet.** Ticked because the build work is done;
      revisit this box if that A/B says otherwise
- [x] **3. Envelope + LFO** — shared ADSR routing (filter/amp/both), LFO →
      pitch and/or filter cutoff.
      Design + build order: [envelope-lfo-design.md](envelope-lfo-design.md).
      Builds clean (Debug + Release, zero warnings), all six build steps
      done, envelope routable Filter/Amp/Both, LFO (triangle/square/S&H)
      routable pitch and/or cutoff independently. **Three things are a
      listening test only you can do, not yet confirmed**: retrigger is
      click-free, S&H sounds stepped not smooth, and LFO→pitch/LFO→cutoff
      stay in lockstep. Ticked because the build work is done; revisit this
      box if any of those three say otherwise
- [x] **4. Mono note handling** — note-priority logic, glide/legato vs.
      retrigger; drive via USB MIDI keyboard or computer-keyboard input.
      Design + build order: [note-handling-design.md](note-handling-design.md).
      Builds clean (Debug + Release, zero warnings), all 8 steps done. Three
      input sources (QWERTY, on-screen keyboard, MIDI) feed one lock-free
      event path → note-priority stack → voice, so priority/glide/legato
      behave identically whichever is used. Three Debug self-tests cover the
      FIFO, the priority stack and MIDI message conversion.
      **Two caveats, neither resolved:**
      (a) **MIDI was never tested against physical hardware** — none was
      available. Conversion logic is self-tested; device enumeration and
      callback registration are not. Re-check when a device is to hand.
      (b) The **last-note vs. highest-note** open decision is deliberately
      *not* settled — it's a live combo box so it can be chosen by ear, per
      architecture.md. Still open there.
- [x] **5. Arpeggiator** — pattern modes (up/down/up-down/random/as-played),
      sample-accurate clock (not `Timer`-based), rate control.
      Design + build order: [arpeggiator-design.md](arpeggiator-design.md).
      Builds clean (Debug + Release, zero warnings), all 7 steps done. Drift-free
      `StepClock`, comparison-key pattern walker (survives a changing held set
      with no index to clamp), a two-deadline sub-block render loop, and a
      hold/latch rule that unions mid-phrase and replaces on a fresh chord.
      Section 7's T1–T10 transition table is now `runArpTransitionSelfTest`,
      driving real blocks through a shared `renderVoiceBlock` rather than a
      re-implementation of the hand-over; it found and fixed two real defects
      (a latched chord surviving arp-off; a step reopening a note on top of an
      already-open gate after a tempo jump) plus a 250-round deterministic
      stuck-note fuzz. Six Debug self-tests total.
      **Two caveats, neither resolved:**
      (a) **Self-test pass/fail is only trustworthy under a debugger (F5)** —
      `jassert` doesn't halt a plain launch, so "it launched and stayed up" is
      not evidence. Affects all six tests, not just the arp's; not yet fixed to
      report headlessly.
      (b) **Untested end-to-end with MIDI hardware and unverified by ear** — the
      listening tests in arpeggiator-design.md section 12 (division timing,
      up-down endpoints, hold rule "feel", whether it's in the SH-101 family)
      are the user's to run.
- [x] **6. UI pass** — knobs/controls for what's built so far, mouse-driven;
      keep touch-first layout decisions in mind even though untested.
      Design + build order: [ui-design.md](ui-design.md); settled decisions
      recorded in architecture.md's "Item 6 — instrument panel" section.
      All 8 build steps done: `PanelLookAndFeel` (flat drawing, no
      gradients/bevels), `ParameterControls.h` (knob/choice/toggle spec
      structs + attach helpers, carried forward from the items 2-5 pattern),
      `PanelSection`, and `SynthPanel` composing all seven sections (VCO,
      VCF, ENV, LFO, KEYBOARD, ARP, OUTPUT) plus a reserved empty SEQ strip
      for item 7, at a fixed 1280x660 design size scaled onto the real
      window via an `AffineTransform` on the panel child. The Claude Design
      canvas mockup (desktop + phone-landscape artboards) came first, per
      the doc's step 1. All 27 parameters (19 knobs, 6 combo boxes, 2
      toggles) present and wired; the six `jassert` self-tests from items
      4-5 survived the rewrite intact. One real defect found and fixed along
      the way: once `SynthPanel` covered the whole window, a click no longer
      landed on `MainComponent` by accident, so it stopped grabbing keyboard
      focus and QWERTY note input silently died after the first click —
      fixed with an explicit `visibilityChanged()` override.
      **Not verified this session**: a Debug + Release rebuild was skipped
      because `Avijiator.exe` was already running (user's own manual test)
      and locking the link step — Debug compiled clean up to that point
      (Main.cpp, MainComponent.cpp, SynthPanel.cpp all built) but the link
      and the Release config weren't re-confirmed here. Worth a rebuild
      before relying on "builds clean".
      **Human-only, not yet done**: whether the panel *looks* right and the
      section grouping is the one that makes sense to play from; whether the
      76px knobs (derived from a 48dp Android touch target, see
      architecture.md) are actually big enough to hit on a phone — no touch
      hardware exists in Stage A, real answer is Stage B item 9; whether the
      arrangement reads as being in the SH-101 family.
- [ ] **7. Step sequencer (phase 2)** — 16-step (page-able) pattern, per-step
      pitch/gate/accent/slide, shared clock/trigger plumbing with the arp.
      **Future consideration, not yet scoped into this item**: per-step
      parameter automation ("p-locks") beyond pitch/gate/accent/slide —
      full design in [step-automation.md](step-automation.md). That doc
      flags itself as plausibly a bigger build than the synth voice, so
      treat it as something to look at when item 7 is underway, not a
      commitment yet
- [ ] **8. Character & "Vim"** — analogue realism + performance-feel layer on
      top of the clean core voice: filter feedback saturation, exponential
      envelope curves, oscillator drift, output noise floor/saturation,
      humanised arp/seq timing, chorus, per-note randomisation, mod
      wheel/aftertouch routing. An *addition* to the core voice, not a
      prerequisite for it — depends on item 2 (voice), item 3 (envelope, for
      A2's curves), and items 5/7 (arp/seq, for B2's humanisation) already
      existing. Gated behind one global VIM switch plus per-feature controls,
      so a clean/clinical mode stays reachable for A/B. Full spec, control
      tiering, and priority order (highest-impact first):
      [character-and-vim.md](character-and-vim.md)

### Voice follow-ups (deferred out of item 2, not numbered — no reordering)

- [ ] **Filter drive / saturation** — a driven stage to colour the sound at
      all levels, for character. Item 2 ships a vanilla signal at normal
      settings so the SH-101 A/B tests one variable at a time; the topology
      is built to take this as a small change. **This is the first thing to
      try if the filter lacks character** — before considering a ladder
      rewrite. **Not the same thing** as the `softClip` already in `Vcf.cpp`
      — that one only engages when resonance pushes the feedback loop past
      self-oscillation, and exists so the filter doesn't diverge to NaN, not
      for flavour. See documents/dsp-voice-design.md section 3
- [ ] **DC blocker after the mixer** — one-pole highpass. A pulse of duty `w`
      carries DC of `2w-1`; real hardware AC-couples it away. Harmless with a
      static pulse width, but **needed before item 3 sweeps PWM with the
      LFO**, or the moving DC thumps

### Tempo sync (not numbered — no reordering)

- [ ] **Internal master tempo for LFO/arp/glide** — one master BPM that LFO
      rate and arp tempo (and, later, the step sequencer) can each
      optionally lock to, with its own division/ratio so the rate scales
      proportionally rather than being forced identical. Nothing exists
      today: each tempo-driven consumer (currently only `Arpeggiator`) owns
      an independent `StepClock` and reads its own raw BPM atomic every
      block. Design rationale and the "this is not host sync" distinction
      are in architecture.md's "Tempo sync" section.
      **Still open:**
      (a) does each existing rate atom (`arpTempoBpm`, `lfoRateHz`) get a
      paired new sync-enable + division field, leaving the raw value as the
      "sync off" fallback — that's what "each with its own division"
      implies, but wants stating explicitly before it's built;
      (b) does glide time meaningfully lock to tempo at all, given it's a
      one-shot transition rather than a periodic rate — two different
      features hide under that one idea ("glide takes 1 beat" vs. "glide's
      seconds value scales with tempo") and neither is committed to yet;
      (c) build the LFO/arp half now, or wait for item 7 (step sequencer) so
      the shared-clock concept only gets factored out once, per
      `StepClock`'s own comment about when that becomes worth doing

### Settings persistence (not numbered — no reordering)

- [ ] **Save/reload all synth settings, including a preset system** —
      nothing is persisted anywhere in this app today: `createStateXml`,
      `ApplicationProperties`, `PropertiesFile`, `ValueTree` all return zero
      hits in `Source/`, and `juce_data_structures` isn't linked yet (same
      gap already flagged in the device-settings item above — linking it
      once serves both). Distinct from, and independent of, that
      device-settings item (audio/MIDI device selection, much narrower) —
      confirmed independent by ui-design.md's own "out of scope" note.
      A preset needs to hold every `VoiceParameters` atomic plus **arp
      performance state**: the latched Hold-mode chord (`Arpeggiator`'s
      `latched`/`numLatched`/`latchAwaitingFreshChord`, see
      arpeggiator-design.md section 8) — e.g. a preset that ships already
      arpeggiating a C-major chord — not raw live key state, which is
      transient and doesn't make sense to "restore".
      A curated collection of good presets could ship permanently with the
      app.
      **Still open:** (a) preset file format — `juce::ValueTree` + XML is
      the natural JUCE-idiomatic pick given `juce_data_structures`, but
      undecided; (b) where shipped presets physically live — embedded as
      `BinaryData` (the same pattern `AvijiatorFonts` already uses,
      immutable with the .exe) vs. written to a user folder at first launch
      (user-editable in place); (c) if tempo sync (above) lands first, its
      new sync-enable/division fields need to be part of what a preset
      saves — not a blocker, just needs revisiting in whichever order these
      two get built

### WAV output / recording (not numbered — no reordering)

- [ ] **Record the live audio output to a WAV file** — nothing exists
      today: `juce_audio_formats` (and `WavAudioFormat`/`AudioFormatWriter`)
      is already linked in `CMakeLists.txt` but has zero uses anywhere in
      `Source/` — this would be its first consumer. Tap point is
      `MainComponent::getNextAudioBlock`, where `renderVoiceBlock` fills the
      mono buffer before it's fanned out to output channels. **Cannot call
      `AudioFormatWriter::write()` directly in the audio callback** —
      CLAUDE.md's hard constraints rule out file I/O there. The pattern to
      follow: `AudioFormatWriter::ThreadedWriter` (lock-free ring buffer +
      background thread doing the real I/O) — the same shape as this
      codebase's existing `NoteEventFifo` (`Source/DSP/NoteEvent.h`), just
      for audio samples instead of note events, and in the opposite
      direction. Start/stop would sit alongside the existing
      `prepareToPlay`/`releaseResources` pair.
      This is **live recording** ("what's currently playing, start/stop
      like a tape recorder") — the synth is played live via MIDI/QWERTY/
      on-screen keyboard with no timeline or song data to bounce, so an
      offline/faster-than-realtime render doesn't apply yet (would wait on
      item 7, the step sequencer, existing first). **Still open:** whether
      this needs an on-screen record button (item 6/UI-pass territory) now,
      or stays a backend-only capability until picked up

### App housekeeping (not numbered — no reordering)

- [ ] **Remember audio/MIDI device settings across restarts** — currently
      `setAudioChannels(0, 2)` picks a default device on every launch (Windows
      falls back to WASAPI unless ASIO is re-selected by hand each time), and
      `enableAllMidiInputs()` just re-enables whatever's currently plugged in
      rather than recalling what was on last time. Nothing is persisted at
      all today — confirmed by grep, zero hits for `createStateXml`,
      `ApplicationProperties`, `PropertiesFile` anywhere in `Source/`.
      Fix is standard JUCE, not novel: `AudioDeviceManager::createStateXml()`
      to save, `initialise (ins, outs, savedXml, true)` to restore (replacing
      the convenience `setAudioChannels` call), stored via
      `juce::ApplicationProperties`. Load in `MainComponent`'s constructor,
      save in its destructor or `AvijiatorApplication::shutdown()`.
      Message-thread/startup-time only — no audio-thread or DSP involvement,
      doesn't block or depend on any numbered item above. Also: link
      `juce_data_structures` explicitly in `CMakeLists.txt` (currently only
      pulled in transitively via `juce_gui_extra`)

## 2. Stage B — Android (port)

- [ ] Install Android SDK/NDK (standalone `cmdline-tools`/`sdkmanager` —
      Android Studio not required just for provisioning)
- [ ] Create a `.jucer` file mirroring the CMake project's module list and
      source files, for Projucer's Android exporter — **note**: this is a
      second project definition to keep in sync with `CMakeLists.txt`
      manually whenever files/modules change
- [ ] Run Projucer's Android exporter → generate the Gradle project
- [ ] **8. Port setup** — confirm the Stage A code builds, installs, and
      makes sound with acceptable latency on the Pixel via `gradlew`/`adb`
      from VS Code's terminal; configure AAudio low-latency (MMAP) path
      explicitly (not automatic)
- [ ] **9. Touch pass** — adapt UI from mouse to touch (tap-to-toggle steps,
      long-press/mode-toggle for accent/slide)
- [ ] **10. Field test** — full rig (phone + hub + interface + MIDI) under
      real playing conditions before relying on it at a gig

## 3. Hardware / live rig

- [ ] Prototype-phase audio out: Behringer UCA202 (cheap, class-compliant,
      sufficient for Stage A/B bring-up)
- [ ] Re-evaluate interface for the final rig at field-test time — balanced
      TRS out (UMC202HD) vs. sticking with the Focusrite Scarlett Solo
- [ ] Confirm hub is genuinely *powered*, not a passive splitter
- [ ] USB class-compliant MIDI keyboard (or Bluetooth MIDI, accepting
      latency/dropout trade-off)
- [ ] Test full power chain (phone + hub + interface + MIDI) under load —
      don't assume stability

## Open questions (carried from architecture.md)

- [ ] Filter topology: SVF cascade vs. ladder emulation — start SVF, revisit
      if it lacks character
- [ ] Note-priority scheme: last-note vs. highest-note — decide by ear
- [ ] Actual cost of the Windows→Android port once Stage A is done — UI
      input handling and audio backend are known rework points, not a
      recompile
