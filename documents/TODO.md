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
- [x] **7. Step sequencer (phase 2)** — 16-step pattern, per-step
      pitch/gate/accent/slide plus two per-step filter lanes (cutoff,
      resonance), shared clock/trigger plumbing with the arp. Design + full
      build-step rationale: [step-sequencer-design.md](step-sequencer-design.md)
      — that doc, not this entry, is where the "why" for each decision below
      now lives.
      All 8 build steps done. `StepSequencer` (`Source/StepSequencer.h/.cpp`)
      owns its own `StepClock` and six per-step `VoiceParameters` arrays
      (pitch/gate/accent/slide + both filter lanes) plus six scalar atomics
      (`seqEnabled`, `seqDivision`, `seqPatternLength`, `seqTempoBpm`,
      `seqGateLength`, `seqRecordArmed`). `process()` copies the arp's
      two-deadline sub-block loop shape rather than sharing it (design doc
      section 2). Accent is realized as velocity into new amp/cutoff depth
      knobs in `SynthVoice`; slide reuses the arp's Tie idea (skip the
      force-close so `SynthVoice` takes its legato branch); the filter lanes
      sum into `SynthVoice`'s existing cutoff modulation point and a new
      resonance one (simple additive 0..1 offset, clamped — section 5).
      Hand-over is a real three-way `VoiceOwner { Keys, Arp, Seq }`
      arbitration in `renderVoiceBlock` (`Arpeggiator.h/.cpp`), all three
      `releaseVoice`s called unconditionally on any change, walked against
      the design doc's S1-S12 transitions table. Live pitch entry
      (`seqRecordArmed`) captures the router's priority-resolved live pick
      into `stepPitchLog2Hz`/`stepGateOn` at each step boundary while armed,
      wrapping rather than auto-stopping (section 8). UI: `SynthPanel`'s
      SEQUENCER section (`designHeight` grew 660 → 840, pre-authorized by
      section 9) replaces the old reserved strip with On+Record, Division,
      Pattern Length, Tempo, Gate, Lane controls and a hand-painted 16-cell
      `StepCell`/`StepGrid` (click=Gate, right-click=Accent,
      shift+click=Slide, vertical drag=selected continuous lane), following
      `PianoKey`'s hand-painted precedent plus new index-based attach helpers
      in `ParameterControls.h`.
      Six self-tests (`runStepSequencerPatternSelfTest`,
      `runStepSequencerRenderSelfTest`, `runAccentDepthSelfTest`,
      `runSeqTransitionSelfTest`, `runFilterAutomationSelfTest`,
      `runStepRecordSelfTest`) drive real audio through the real classes
      rather than re-implementing what they check. Builds clean (Debug +
      Release, zero warnings) throughout every build step; verified via
      `cdb.exe` — no assertion fired from any step-sequencer or arp/note-
      router self-test, only the unrelated font bug below. Visually verified
      from a real cold-start launch: correct layout and correct control
      defaults.
      **Not verified by a human**: the grid's mouse gestures
      (click/drag/right-click/shift-click) and live-record — synthetic input
      in this environment proved unreliable for testing them, so both need
      clicking/playing by hand before being trusted; MIDI-hardware
      end-to-end remains untested (no device available this session);
      whether any of it sounds like an acid line is an ears judgement
      nobody has made yet (design doc section 12's "Taste" list).
      **Future consideration, not yet scoped into this item**: *generalised*
      per-step parameter automation ("p-locks") for arbitrary parameters
      beyond pitch/gate/accent/slide/cutoff/resonance — full design in
      [step-automation.md](step-automation.md). That doc flags itself as
      plausibly a bigger build than the synth voice, so treat it as
      something to look at now that item 7 is built, not a commitment yet
- [x] **8. Autoviji — random sequence fill** — one-press "surprise me" button
      in the header row, left of Audio Settings. Design record, written after
      the fact rather than before (session discipline note - should have been
      step 0): [autoviji-design.md](autoviji-design.md).
      Fills all 16 steps with a random note across two fixed octaves (C2..B3,
      MIDI 36-59), independently rolls each step's gate with a 1-in-8 chance
      of coming up off, and randomizes each step's per-step Cutoff lane
      (`stepCutoffNorm`, item 7's existing modulation lane - not the main VCF
      Cutoff knob). Also sets Autoviji's own default groove (Division 1/8T,
      Pattern Length 8) and turns the sequencer on, syncing all four affected
      widgets (step grid, On toggle, Division combo, Pattern Length combo) to
      match the atomics it just wrote. `SynthPanel::randomizeSequence()`
      (`Source/UI/SynthPanel.cpp`), reusing the exact plain-atomic
      `storeStepValue` helper `StepCell::mouseDrag`/`mouseUp` already use for
      these fields (`ParameterControls.h`) - no new threading pattern, runs
      entirely on the message thread as a button click handler. Button text
      is amber (`PanelLookAndFeel::accentAlt`), matching the knob pointer
      colour it shares a palette role with.
      Verified via a real Debug build + launch: random notes land inside the
      intended range, roughly 1-in-8 steps come up off, Division/Pattern
      Length/On all update visibly, and unrelated knobs are untouched.
      **One caveat from that verification**: several early test runs showed
      unrelated knobs (Saw, Pulse, Cutoff, Resonance, LFO...) appearing to
      change too - traced to leftover `Avijiator.exe` processes from earlier
      in the session being captured instead of the real window, not a real
      bug; resolved once every stray process was killed before testing. Not
      re-verified against a MIDI hardware or on a full `cdb.exe` pass since
      the octave-range/gate-odds edit.
      **Regression found and fixed 2026-09-01**: the Division combo showed
      NO selection after clicking Autoviji - found by the user, not by any
      self-test (this button's own widget-sync line has never had one - see
      `randomizeSequence`'s comment). Root cause: `randomizeSequence` set
      `seqChoices[0].comboBox`'s selected id to
      `(int) StepDivision::EighthTriplet + 1`, the TRUE global enum value -
      correct before tempo-sync-design.md's follow-up work sliced seq's
      Division combo (`seqChoiceSpecs`' own Division entry now sets
      `firstChoiceValue = StepDivision::Whole`, so combo-local ids are offset
      from the enum's true values). `EighthTriplet + 1` no longer matched any
      real item in the sliced list, so JUCE quietly showed no selection -
      no crash, no assertion, silent. `params.seqDivision` itself was never
      wrong (stored the correct atomic value throughout) - purely a display
      desync. Fixed by subtracting `seqChoiceSpecs[0].firstChoiceValue` back
      off before the `+1`, mirroring `attachChoice`'s own seeding formula
      (`ParameterControls.h`). Verified via build + `cdb.exe` (no assertion
      hits, though this class of bug wouldn't have fired one anyway - it's a
      display bug, not a crash); **not yet re-confirmed visually** that the
      Division combo now shows "1/8T" after clicking Autoviji - that's the
      user's own eyes to make.
- [x] **9. Settings Persistence** — Design + build order:
      [settings-persistence-design.md](settings-persistence-design.md). Built
      2026-09-01, all 7 build steps done. Two tiers sharing one
      `PresetSerialization::toXml`/`fromXml` pair: Tier A (silent
      session-state auto-restore, `MainComponent`'s `appProperties`, a
      `synthState` key alongside the existing `audioDeviceState` one) and
      Tier B (named presets — Save/Load buttons in `SynthPanel`'s header row,
      between `autovijiButton` and `audioSettingsButton` per this session's
      placement call — one `.avipreset` XML file per preset in a `Presets/`
      folder next to the settings folder, plus a small factory-written
      starting pair on first launch). Covers every `KnobSpec`/`ChoiceSpec`/
      `ToggleSpec`-covered scalar (via a new `SynthPanel::
      forEachSerializableParameter` generic enumeration), 4 hand-written
      scalars, the 6×16 step-sequencer arrays, and the arp's latched
      (Hold-mode) chord via new lock-free save/load plumbing on
      `Arpeggiator` (`getLatchSnapshot`/`requestLatchLoad`).
      Two deviations from the design doc, both found via this project's cdb
      self-test convention rather than foreseen in the doc: the generic
      enumeration keys each field `"<table>.<display name>"` instead of a
      hand-typed `serializedName` per entry (same uniqueness guarantee, no
      new field to type — and risk skipping — on ~33 existing initializers);
      and each field serializes as a `<Param key="..." value="..."/>` child
      element rather than a plain XML attribute, since several display names
      (`"Pulse Width"`, `"Env->Cutoff"`, ...) aren't valid XML attribute
      *names* — a real `JUCE_ASSERT` the first cdb run caught, not a
      hypothetical. See `settings-persistence-design.md` sections 4-6's own
      "Built" notes for the full record. Verified via build, the cdb
      self-test convention (0 assertion hits, including the new
      `runPresetRoundTripSelfTest`), and a real end-to-end run confirming the
      `Presets/` folder, both factory presets, and the `synthState` key all
      wrote correctly to `%APPDATA%/Avijiator/`.
- [ ] **10. Character & "Vim"** — analogue realism + performance-feel layer on
      top of the clean core voice: filter feedback saturation, exponential
      envelope curves, oscillator drift, output noise floor/saturation,
      humanised arp/seq timing, chorus, per-note randomisation, mod
      wheel/aftertouch routing. An *addition* to the core voice, not a
      prerequisite for it — depends on item 2 (voice), item 3 (envelope, for
      A2's curves), and items 5/7 (arp/seq, for B2's humanisation) already
      existing. Gated behind one global VIM switch plus per-feature controls,
      so a clean/clinical mode stays reachable for A/B. Full spec, control
      tiering, and priority order (highest-impact first):
      [character-and-vim.md](character-and-vim.md). **Filter drive /
      saturation** (a driven stage to colour the sound at all levels, folded
      in from the former "Voice follow-ups" list) belongs here too — **the
      first thing to try if the filter lacks character**, before considering
      a ladder rewrite. Item 2 ships a vanilla signal at normal settings so
      the SH-101 A/B tests one variable at a time; the topology is built to
      take this as a small change. **Not the same thing** as the `softClip`
      already in `Vcf.cpp` — that one only engages when resonance pushes the
      feedback loop past self-oscillation, and exists so the filter doesn't
      diverge to NaN, not for flavour. See documents/dsp-voice-design.md
      section 3.
      **V1 built 2026-09-03** (see character-and-vim.md's "V1 build scope"
      section for the curated cut this covers — the box above stays
      unchecked because it still lists the FULL spec, and v1 is deliberately
      a subset): `Vcf::driveSaturate` (A1 — pushes the existing feedback-loop
      `softClip` harder via a caller-supplied 0..1 drive, exactly
      `softClip(x)` at 0); `Adsr::setCurveEnabled` (A2 — exponential
      Decay/Release, gated by the new `vimEnabled` atomic, linear/unchanged
      when off); `Humanise::onsetDelaySamples`/`velocityJitterFactor`
      (B2 — one `humaniseAmount` knob driving swing + timing jitter + velocity
      jitter, applied as a third scheduling deadline in both
      `Arpeggiator::process` and `StepSequencer::process`, exactly inert at
      0); and a new hand-rolled `Chorus` block (B1 — fixed-rate/depth
      modulated-delay ensemble, replacing `MainComponent`'s mono-to-stereo
      copy when `chorusEnabled` is on). New `CHARACTER` panel section (VIM +
      Chorus toggle stack, Drive knob, Humanise knob) sits between SEQUENCER
      and OUTPUT, picked up by preset save/load automatically (`SynthPanel::
      forEachSerializableParameter`, no separate serializer change needed).
      Seven new Debug self-tests (`runAdsrCurveSelfTest`,
      `runVcfDriveSelfTest`, `runChorusSelfTest`, `runHumaniseFormulaSelfTest`,
      `runArpHumaniseSelfTest`, `runSeqHumaniseSelfTest`, plus the existing
      six survived unchanged) prove each new mechanism inert at its knob's
      default and, for the humanise scheduling specifically, that a pending
      onset dropped by a hand-over never fires late — one real defect
      (`Arpeggiator`/`StepSequencer::process` first drafts checked a
      not-yet-fired scheduled note for sound one block too early) found and
      fixed by that self-test rather than by ear. Builds clean (Debug +
      Release, zero warnings); verified via the `cdb.exe` convention — 0
      assertion hits across all 15 self-tests — and a real launch (Debug and
      Release) with no crash.
      **Rest of Tier 1 built 2026-09-05** (same session's follow-up, after
      the user asked "how much to do those other things" and confirmed):
      `OscillatorDrift` (A3 — a bounded leaky-integrator random walk, NOT a
      free-running accumulator; the main oscillator's own instance lives in
      `SynthVoice` as an additive-octaves pitch term, exactly like every
      other pitch modulator there, while the sub-oscillator gets its own,
      independently-seeded instance living INSIDE `PolyBlepOscillator`
      itself, added as a small bounded phase offset rather than a frequency
      offset — sub's phase is deliberately derived from the main phase, not
      accumulated separately, so it has no frequency of its own to offset;
      see that class's own comment for why a bounded phase wobble doesn't
      reintroduce the free-running-accumulator beating bug that comment
      already warns against); `curvedVelocity` (A6 — a simple `x^2` shape
      applied to `currentVelocity` before it reaches SynthVoice's two
      existing velocity summing points; scope narrowed to velocity's own
      response only, not modulation-depth knob curves too — see the design
      doc's build record for why); and `CharacterProcessor` (B4 — noise floor
      *and* output soft-clip/asymmetric-clipping in one small output-stage
      block, owned by `MainComponent`, applied to the mono mix right after
      `renderVoiceBlock` and before `Chorus`). All three gated by the SAME
      `vimEnabled` read already wired for A2 — no new controls, exactly Tier
      1's own "one switch, no sub-parameters" design. Two design questions
      (asked of, and settled by, the user rather than guessed at) are on
      record in character-and-vim.md's build note: sub-oscillator drift's
      bounded-phase-wobble approach, and A5 (component-bleed) staying
      deferred as too vague to build safely this session.
      Four more Debug self-tests (`runOscillatorDriftSelfTest`,
      `runCharacterProcessorSelfTest`, and `runVimCharacterSelfTest`'s
      integration proof through `SynthVoice`, alongside the four above) — 18
      total now, all clean via `cdb.exe`. Builds clean (Debug + Release, zero
      warnings); Release launched and stayed up with no crash.
      **Deliberately deferred, not forgotten** (see the design doc's own
      scope-cut list): A5 (component-bleed — explicitly too vague to build
      safely, see above), oversampling (A4), per-note randomisation (B3), mod
      wheel/aftertouch routing (B5 — no MIDI CC/aftertouch plumbing exists
      yet), FM/ring-mod (B6), and Chorus/Humanise's own Rate/Depth/
      Swing-Timing-Velocity split into separate knobs (both v1 scope-cuts,
      172px of row slack already budgeted for exactly this).
      **Human-only, not yet done**: whether ANY of this — drive, exponential
      envelopes, chorus, humanised timing, oscillator drift, the velocity
      curve, output noise floor/saturation — actually sounds "in the family"
      with an SH-101/analogue reference, or just different. CLAUDE.md's "what
      you cannot verify" section reserves that listening test for the user;
      it applies to this item more than most. Also untested against MIDI
      hardware this session.

### Voice follow-ups (deferred out of item 2, not numbered — no reordering)

- [ ] **DC blocker after the mixer** — one-pole highpass. A pulse of duty `w`
      carries DC of `2w-1`; real hardware AC-couples it away. Harmless with a
      static pulse width, but **needed before item 3 sweeps PWM with the
      LFO**, or the moving DC thumps

### Tempo sync (not numbered — no reordering)

- [x] **Internal master tempo for LFO/arp/glide** — Design + build order:
      [tempo-sync-design.md](tempo-sync-design.md). Built 2026-08-30, all 5
      build steps done. `VoiceParameters::masterTempoBpm` replaces
      `arpTempoBpm`/`seqTempoBpm` (mechanical rename across
      `Arpeggiator.cpp`/`StepSequencer.cpp` and their self-test rigs); arp
      and sequencer each keep their own `StepClock` instance and Division
      atomic — only the BPM *source* is shared. SEQUENCER's own Tempo knob
      was removed (`numSeqKnobs` 2→1, no width-budget impact — that row
      isn't budgeted). The Tempo knob itself first landed in ARP (repointed
      to `masterTempoBpm`, label/position unchanged, flagged as the simplest
      placement rather than an argued one), then moved to OUTPUT next to
      Level on 2026-08-30 once the user judged ARP misleading for a
      global-tempo knob (`numArpKnobs` 2→1, `numOutputKnobs` 1→2).
      LFO gains `lfoSyncEnabled`/`lfoSyncDivision` atomics and a
      `SynthVoice.cpp` computation (right at the existing `lfo.setRate` call
      site) that reuses `StepClock.h`'s `beatsPerStepForDivision` table
      as-is — no new Hz-from-BPM-and-division table needed. New Debug
      self-test `runLfoTempoSyncSelfTest`: proves the sync-off path is
      byte-identical to free-run regardless of tempo/division, and proves
      two known BPM/division pairs (300 BPM/1-16 → 20 Hz; 240 BPM/1-8 →
      8 Hz) land the LFO's own Square-wave half-period at the arithmetically
      exact sample boundary, via a loud/quiet cutoff-driven amplitude
      pattern (LFO → cutoff, wide depth, clamped by Vcf's own [20, 18000] Hz
      range at both ends) rather than reaching into `Lfo`'s private phase.
      Free-run mode's own, independent ask: `lfoKnobSpecs`' Rate floor
      widened 0.02 → 0.005 Hz (~200s cycle) with a geometric-mean skew
      (0.3162), matching Cutoff/Attack/Decay/Release's existing convention -
      no self-test, knob-spec/layout only.
      UI: LFO gained its first-ever toggle (Sync, a plain `ToggleButton` in
      its own cell, not a stack) and a second combo (Sync Division, reusing
      `arpDivisionChoices` as a third user of that shared table). Row B's
      `designWidth` grew 1280→1456 (exactly the two new LFO cells' worth of
      pixels) to avoid robbing KEYBOARD/ARP/OUTPUT; the Tempo move to OUTPUT
      left row B's total unchanged (a knob cell moved sections, none added).
      Row A's own `envRowWidthCompensation` was initially left unrebalanced
      (flagged as a gap on row A's right), then fixed per the "line up the
      panels" request: 28px → 204px, so row A once again totalled row B's
      1416px.
      **Second UI follow-up, same day**: OUTPUT moved off row B onto its own
      row directly under ARP (`outputSection.setBounds` positioned at
      `arpSection`'s own X, not routed through the row's left-to-right
      `place` helper - it isn't sharing a row with anything). Row B dropped
      back to 3 sections (LFO|KEYBOARD|ARP), and by coincidence its new
      total cell count (13, across 3 sections) exactly matches row A's
      (13, across 3 sections too) - `envRowWidthCompensation` went back to
      0 (not deleted - one edit away if either row's count changes again).
      `designWidth` shrank 1456→1252 (row B no longer needs OUTPUT's width);
      `designHeight` grew 840→1004 (one new section row plus its gap).
      **Third UI follow-up, 2026-08-31**: OUTPUT moved again, off its own row
      onto SEQUENCER's existing row, immediately to SEQUENCER's right - back
      through the ordinary `place` left-to-right helper, since it's sharing
      a row again. SEQUENCER's row was never width-budgeted (natural width,
      left-aligned - same as before OUTPUT joined it), so no width rebalance
      needed; `designHeight` dropped back 1004→840 now that OUTPUT no longer
      needs its own row. `designWidth` stayed at 1252 throughout this one -
      SEQUENCER+OUTPUT's combined natural width is nowhere near the budget
      row A/B set.
      `static_assert` control-count totals updated: 7 combo boxes (was 6), 3
      toggles (was 2) — the 19-knob total
      is unchanged throughout (Tempo/OUTPUT moving sections doesn't change
      it).
      Builds clean (Debug + Release, zero warnings) at every step, including
      all three UI follow-ups; verified via `cdb.exe` after each — no
      assertion hits.
      **Was open, now settled** (see the design doc's intro for the
      reasoning):
      (a) settled — one shared dial (masterTempoBpm), not a per-consumer
      sync-enable + fallback;
      (c) settled — item 7 is done, so `StepClock`'s own "wait for a second
      owner" condition is already satisfied.
      **Still open, deliberately** (moved to architecture.md's Tempo sync
      section too): (b) does glide time meaningfully lock to tempo at all,
      given it's a one-shot transition rather than a periodic rate — two
      different features hide under that one idea ("glide takes 1 beat" vs.
      "glide's seconds value scales with tempo") and neither is committed to
      yet; left for a future item, out of the design doc's scope.
      **Human-only, not yet done**: whether the synced LFO actually feels
      locked to the beat by ear; whether the widened free-run floor is
      usefully slower in practice; whether OUTPUT sitting next to SEQUENCER
      reads well now that the panel is back to its pre-follow-up height. Not
      yet tested against MIDI hardware.

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

- [x] **Fix silent font-loading failure** — Found 2026-08-27 via `cdb.exe`:
      JUCE's assertion at `fonts/juce_FontOptions.h:138` fired on every
      repaint (~250 times in a 20s run). Original note (written before
      investigation) guessed this meant `createSystemTypefaceFor` was
      returning null. **That guess was wrong** — fixed 2026-08-29 after
      checking the actual assert (`x == nullptr || style.isEmpty()`) and
      confirming with cdb (breaking on the assert and inspecting the `x`
      argument directly) that the typeface pointer was always non-null. The
      real cause: every call site built fonts as
      `FontOptions(height).withTypeface(typeface)` — `FontOptions(height)`
      already carries a non-empty default "Regular" style (from
      `Font::plain`), which `withTypeface()` asserts should be empty before
      silently discarding it. The embedded IBM Plex Sans fonts were loading
      correctly the whole time; this was a noisy but harmless Debug-only
      assertion, not a rendering bug. Fix: added
      `PanelLookAndFeel::fontFor(typeface, height)`, which builds
      `FontOptions(typeface).withHeight(height)` instead (no field to
      discard), and switched all 8 call sites (`PanelLookAndFeel.cpp`,
      `SynthPanel.cpp`) to use it. Also added `jassert(typeface != nullptr)`
      inside `regularTypeface()`/`semiBoldTypeface()` so a *genuine* future
      load failure (e.g. a real BinaryData/resource mismatch) fails loudly
      instead of silently falling back, which is what this item's title
      actually asked for. Verified clean via the cdb self-test (0 assertion
      hits, down from ~250) after a rebuild.
- [x] **Remember audio/MIDI device settings across restarts** — done
      2026-08-29. Turned out simpler than this note's original sketch:
      `AudioAppComponent::setAudioChannels` already takes an optional
      `const XmlElement*` third argument that does both the
      `deviceManager.initialise(...)` call *and* the `AudioSourcePlayer`
      wiring `setAudioChannels` normally does, so the fix was passing the
      saved state straight into the existing call rather than swapping it
      for a raw `deviceManager.initialise(...)`. Saved/loaded via
      `juce::ApplicationProperties` (`MainComponent`'s constructor loads,
      destructor saves via `createStateXml()`), `juce_data_structures` now
      linked explicitly in `CMakeLists.txt`. `enableAllMidiInputs()` is now a
      first-launch-only fallback — JUCE's saved device XML already restores
      which MIDI inputs were enabled, so it's skipped once state exists.
      Verified end-to-end (not just built): launched, quit gracefully,
      confirmed `%APPDATA%/Avijiator/Avijiator.settings` was written with
      the live `DEVICESETUP`/`MIDIINPUT` XML; relaunched and quit again,
      confirmed the file round-tripped cleanly with no corruption or
      duplication. cdb self-test clean (0 assertions) both before and after.
- [x] **Step sequencer's default pitch is an audible frequency,
      not "off"** — `VoiceParameters::stepPitchLog2Hz` (`Source/DSP/VoiceParameters.h`)
      is a zero-initialized `std::array`, the same convention every other
      per-step field uses (gate, accent, slide, the cutoff/resonance lanes),
      where `0` correctly means "no effect". Pitch is stored as `log2(Hz)`
      though, not a depth or a flag, so its zero-init isn't neutral - it's a
      real value, `2^0 = 1 Hz`, roughly five octaves below anything reachable
      via the keyboard (`VoiceParameters::minMasterOctaveShift = -2` floors
      out at MIDI 24, ≈33 Hz). Found 2026-08-28 via a user bug report ("sequencer
      just clicks, keyboard plays a nice clear note") that took most of a
      session to trace: a step whose gate is switched on by a plain click in
      the grid (`SynthPanel.cpp`'s `StepCell::mouseUp`, which only ever
      touches `stepGateOn`) but whose pitch is never separately dragged plays
      that inaudible 1 Hz "note" - heard as a click-train
      (`documents/step-sequencer-design.md` section 8 territory), not a
      bug in the render loop itself, which was directly verified correct via
      a real repro through `renderVoiceBlock`. Invisible on a fresh pattern
      because every step's gate also defaults off, so nothing plays the
      nonsense pitch underneath until a step gets gated without ever being
      pitched. Candidate fixes, not decided: seed `stepPitchLog2Hz` to a
      sane note (e.g. middle C) instead of `0`; or have the plain-click
      gate-on gesture snap pitch up to a default if it's still at the raw
      zero. Out of scope for whichever numbered item is active when this is
      picked up
- [ ] **Octave transpose promoted to one shared, global control** — moved
      from `QwertyNoteInput`-owned + `SynthPanel`-mirrored dual state to
      `VoiceParameters::masterOctaveShift`, applied uniformly to QWERTY,
      on-screen keyboard, MIDI hardware input, the arpeggiator, and the step
      sequencer (previously keyboard-input-only — MIDI/arp/seq scope is a
      deliberate widening, not a bug fix). Control moved from its standalone
      placement next to the on-screen keyboard into the OUTPUT panel,
      alongside Level/Tempo, and rebuilt as a detented rotary knob (matching
      Level/Tempo's own look) rather than the two-button widget it started
      as. See `documents/note-handling-design.md` section 7's revision for
      the full design record.

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
