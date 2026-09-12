/*
    This file is part of Avijiator.
    Copyright (C) 2026 Nick Casey

    Avijiator is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version.

    Avijiator is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with Avijiator. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <array>
#include <atomic>

#include "Lfo.h"
#include "NoteEvent.h"
#include "NoteStack.h"
#include "StepClock.h"

// For ArpPattern. The arpeggiator is a PEER class, not part of the voice, so
// this is admittedly the wrong direction for a DSP header to point - see the
// note above the item 5 block below. No cycle: Arpeggiator.h forward-declares
// SynthVoice and includes nothing from here.
#include "../Arpeggiator.h"

// For seqMaxSteps, sizing the pattern-storage arrays below. Same "wrong
// direction, no cycle" situation as the Arpeggiator include just above:
// StepSequencer is a peer class too, and StepSequencer.h includes nothing
// from here.
#include "../StepSequencer.h"

//==============================================================================
/*
    Whether an overlapping note-on restarts the envelope.

    Governs note-ON only. A note-off that reveals another still-held note
    underneath is NEVER a retrigger in either mode - nothing was newly
    pressed - which is why SynthVoice has a separate retargetPitch() method.
    See documents/note-handling-design.md section 5.
*/
enum class LegatoRetriggerMode : int { Retrigger = 0, Legato = 1 };

//==============================================================================
/*
    Where the shared envelope's output is routed - CLAUDE.md's hard constraint
    is ONE envelope, switchable between destinations, not a second envelope.
    Stored as a plain std::atomic<int>, no smoother: it's a discrete switch,
    not a continuous value - see documents/envelope-lfo-design.md section 5.
*/
enum class EnvelopeDestination : int { Filter = 0, Amp = 1, Both = 2 };

//==============================================================================
/*
    Every UI -> audio parameter for the voice lives here as an atomic, per
    CLAUDE.md: UI -> audio changes go through atomics, never a shared mutable
    field. SynthVoice snapshots these once per block; continuous values are
    then smoothed per sample, discrete switches and time constants are read
    raw - see documents/dsp-voice-design.md section 4 and
    documents/envelope-lfo-design.md section 5 for which is which and why.

    Grew incrementally as each build step landed its oscillator/filter tap
    (item 2, documents/dsp-voice-design.md section 7) and then its envelope/
    LFO parameter (item 3, documents/envelope-lfo-design.md section 7). Item
    4 (note handling) is next to add to this struct.
*/
struct VoiceParameters
{
    // Housekeeping (documents/TODO.md): every other field in this struct gets
    // its default from an in-class initializer, but stepPitchLog2Hz can't -
    // an array of std::atomic<float> has no way to aggregate-init every
    // element to the same NON-zero value. This constructor exists for that
    // one field alone; everything else still uses in-class initializers as
    // normal. See stepPitchLog2Hz's own comment for why 0.0f (silence-range
    // pitch, an artifact of zero-init) was the wrong default in the first
    // place.
    VoiceParameters()
    {
        for (auto& step : stepPitchLog2Hz)
            step.store (pitchLog2HzForMidiNote (defaultStepMidiNote), std::memory_order_relaxed);
    }

    // NOTE: there is no pitch parameter here any more. Pitch now arrives as
    // note events through NoteRouter's FIFOs and is owned by SynthVoice's
    // Glide, not set from the UI - see documents/note-handling-design.md
    // section 6.
    std::atomic<float> sawLevel { 0.70f };
    std::atomic<float> pulseLevel { 0.00f };

    // Duty cycle, 0..1. The oscillator clamps this against the current phase
    // increment, so the usable range narrows toward 0.5 at high pitch.
    std::atomic<float> pulseWidth { 0.50f };

    std::atomic<float> subLevel { 0.00f };
    std::atomic<float> noiseLevel { 0.00f };

    // Also log2(Hz), for the same reasons as pitch - and so envelope and LFO
    // modulation can sum in octaves, which is the only way a modulator sounds
    // the same at 200 Hz as at 5 kHz. Default is log2(2000).
    std::atomic<float> cutoffLog2Hz { 10.9658f };

    // 0..1, mapped onto the filter's feedback gain. Self-oscillates near the
    // top of the range.
    std::atomic<float> resonance { 0.20f };

    std::atomic<float> outputLevel { 0.25f };

    // NOTE: item 3's `gate` atomic is gone. Keeping it alongside real note
    // input would mean two independently edge-detected triggers of the same
    // envelope with no defined precedence - the exact race the note FIFO
    // exists to eliminate. Its removal is a correctness requirement, not
    // tidying - see documents/note-handling-design.md section 6.

    // Envelope times: NOT smoothed by SynthVoice (see envelope-lfo-design.md
    // section 5) - changing one only affects the rate of future samples, not
    // the current output value, so there's no click to smooth away.
    std::atomic<float> attackSeconds { 0.01f };
    std::atomic<float> decaySeconds { 0.1f };
    std::atomic<float> releaseSeconds { 0.3f };

    // Directly assigned as the envelope's output level during Sustain, so
    // (unlike the times above) this one IS smoothed - an unsmoothed jump here
    // would be a real, audible pop.
    std::atomic<float> sustainLevel { 0.7f };

    // Default is Amp: the safest first-load behaviour - the voice goes
    // silent when not gated, rather than continuing to drone like item 2
    // left it. Starting pick, easy to change by ear.
    std::atomic<int> envelopeDestination { (int) EnvelopeDestination::Amp };

    // Octaves. Directly scales a modulation amount every sample, so - unlike
    // the envelope times above - this IS smoothed: a depth jump would be an
    // audible pop.
    std::atomic<float> envToCutoffDepthOctaves { 0.0f };

    // Hz. NOT smoothed - same time-constant reasoning as the envelope times
    // above: changing the rate only affects the LFO's future phase
    // increment, not its current output value.
    std::atomic<float> lfoRateHz { 2.0f };

    // Discrete, no smoother - matches envelopeDestination's treatment.
    std::atomic<int> lfoWaveform { (int) Lfo::Waveform::Triangle };

    // Tempo sync (documents/tempo-sync-design.md). Discrete, no smoother -
    // same treatment as lfoWaveform just above. Off on first load: the LFO
    // must free-run at lfoRateHz out of the box, same "must play normally"
    // reasoning as arpEnabled/seqEnabled.
    std::atomic<int> lfoSyncEnabled { 0 };

    // Which StepDivision the synced LFO locks to - reuses StepClock's own
    // beat-ratio table (StepClock.h) rather than a second one, per the design
    // doc's section 3. Only consulted while lfoSyncEnabled is set.
    std::atomic<int> lfoSyncDivision { (int) StepDivision::Sixteenth };

    // Octaves. Directly scale a modulation amount every sample, so - like
    // envToCutoffDepthOctaves above - these ARE smoothed.
    std::atomic<float> lfoToPitchDepthOctaves { 0.0f };
    std::atomic<float> lfoToCutoffDepthOctaves { 0.0f };

    // Item 7, build step 3 (accent DSP). currentVelocity has been captured
    // since item 4 but never routed - SynthVoice.cpp:183-188 and
    // SynthVoice.h's own comment on that field already named these two as
    // its eventual landing points. Both default to 0 (inert): a patch that
    // never touches them renders byte-identically to before this step, the
    // same guarantee envToCutoffDepthOctaves/lfoTo*DepthOctaves above give -
    // see documents/step-sequencer-design.md section 12. Not yet reachable
    // from the panel - the UI knob is step 6's job, since exposing it means
    // rebalancing the fixed-width row SynthPanel.cpp's resized() already
    // fills exactly (see that file's row-B comment); until then these are
    // set only by runAccentDepthSelfTest and, eventually, by hand.

    // 0..1, not octaves - multiplicative, same units as the summing point it
    // feeds (SynthVoice.cpp's amplitudeModulation, itself multiplicative and
    // unity-defaulted). 0 = no velocity sensitivity, every note-on plays at
    // full amplitude regardless of velocity; 1 = amplitude tracks velocity
    // exactly. Smoothed - a depth change is a step change in gain otherwise.
    std::atomic<float> velocityToAmpDepth { 0.0f };

    // Octaves, additive into the existing cutoffModulationOctaves sum
    // (SynthVoice.cpp:214-218) - same units as envToCutoffDepthOctaves/
    // lfoToCutoffDepthOctaves just above, for the same reason: a modulator
    // that shifts cutoff by a fixed number of Hz sounds different at 200 Hz
    // than at 5 kHz. Reference point is velocity == 1.0 (no shift) - every
    // note source that doesn't vary velocity (QWERTY, the arp, a full-
    // velocity MIDI note, a sequencer step's own accent) already plays at
    // exactly 1.0, so only a velocity BELOW that - a normal, un-accented
    // sequencer step, or a soft MIDI note-on - pulls the cutoff down.
    std::atomic<float> velocityToCutoffDepthOctaves { 0.0f };

    //==============================================================================
    // Item 4 (note handling).

    // Discrete, no smoother - matches envelopeDestination and lfoWaveform.
    // Retrigger by default: it continues item 3's already-ear-verified
    // behaviour, where every note-on plucks. Legato is the opt-in mode.
    std::atomic<int> legatoRetriggerMode { (int) LegatoRetriggerMode::Retrigger };

    // Discrete, no smoother. architecture.md leaves last-vs-highest note
    // priority explicitly open ("try both by ear"), so this is a runtime
    // switch and LastNote is only a provisional pick - the near-universal
    // mono-synth default, including the SH-101's.
    std::atomic<int> notePriorityMode { (int) NotePriorityMode::LastNote };

    // Seconds to glide ONE OCTAVE - a rate, not a per-interval duration. Time
    // constant, so NOT smoothed, same as the ADSR times and lfoRateHz.
    // 0 = instant/off, which is the safe first-load default.
    std::atomic<float> glideTimeSeconds { 0.0f };

    //==============================================================================
    // Tempo sync (documents/tempo-sync-design.md). Replaces the two
    // independent atomics arpTempoBpm/seqTempoBpm used to each own below -
    // one shared dial, per the design doc's decision (a). Arp and sequencer
    // keep their own Division atomics untouched; only the BPM source is now
    // shared. A TIME CONSTANT, raw per block, same reasoning as the two
    // atomics it replaces - read once per block by Arpeggiator::process and
    // StepSequencer::process, each still calling its own StepClock::setTempo
    // independently.
    std::atomic<float> masterTempoBpm { 120.0f };

    //==============================================================================
    // One shared octave transpose, applied uniformly to every note source
    // (Keys - QWERTY/on-screen/MIDI - Arp, Seq), read once per note-on/
    // retarget/step by each. Replaces the old scheme where QwertyNoteInput
    // owned this and SynthPanel kept an explicitly-not-source-of-truth
    // mirror (documents/note-handling-design.md section 7's revision).
    // Discrete, whole-octave steps - not smoothed, same treatment as
    // notePriorityMode above. See adjustMasterOctaveShift below, which is
    // what comma/period and the OUTPUT panel's Octave buttons both call.
    static constexpr int minMasterOctaveShift = -2;
    static constexpr int maxMasterOctaveShift = 4;
    std::atomic<int> masterOctaveShift { 0 };

    //==============================================================================
    // Item 5 (arpeggiator).
    //
    // Strictly NOT voice parameters - the arp is a peer class owned by
    // MainComponent and touches no DSP - but they share the one UI -> audio
    // channel, and MainComponent's spec tables are typed against this struct.
    // Splitting them out would buy purity and cost a second spec table; item
    // 6's real UI pass is the place to revisit that. See
    // documents/arpeggiator-design.md section 9.
    //
    // None of the six is smoothed, and each was checked against the three
    // conventions above rather than assumed.

    // Discrete switches, raw per block - identical treatment to
    // envelopeDestination, lfoWaveform and notePriorityMode. Off on first
    // load: the instrument must play normally out of the box.
    std::atomic<int> arpEnabled { 0 };
    std::atomic<int> arpPattern { (int) ArpPattern::Up };
    std::atomic<int> arpDivision { (int) StepDivision::Sixteenth };
    std::atomic<int> arpHold { 0 };

    // A FRACTION of the step, not a time, so changing tempo does not also
    // change articulation. Read at exactly one instant per step, so likewise
    // unsmoothed. Clamped to 0.05-0.95 by the arpeggiator: 100% is not "a
    // longer gate" but a different feature - see section 10.
    std::atomic<float> arpGateLength { 0.5f };

    //==============================================================================
    // Item 7 (step sequencer).
    //
    // Peer class parameters, same reasoning as item 5's block above: the step
    // sequencer is a peer class owned by MainComponent, not part of the
    // voice, but shares this one UI -> audio channel.
    //
    // Struct-of-arrays, not the member-pointer spec shape items 2-6 used
    // everywhere else: ParameterControls.h's KnobSpec/ChoiceSpec/ToggleSpec
    // are all "one pointer-to-member per control" and cannot address "array +
    // index" - a new index-based attach helper is build step 6, not this
    // one. See documents/step-sequencer-design.md sections 1 and 4.
    //
    // Read by index at each step boundary, one read per field per step. A
    // torn read across fields is harmless - the whole step is consumed
    // together at one instant - same "raw atomics, no smoothing at the point
    // of read" convention as masterTempoBpm/arpGateLength above.

    // Log2(Hz), same convention as pitch everywhere else in this file - so a
    // step's pitch sums in octaves with everything else already built that
    // way. NORMALLY message-thread-written (a grid edit) like every other
    // field in this block, but build step 7 (pitch entry) gives this ONE
    // array a second writer: StepSequencer::process, on the AUDIO thread,
    // while seqRecordArmed is on - see that atomic's own comment below and
    // documents/step-sequencer-design.md section 8. Never written by both
    // threads at the same instant in practice (recording a step and
    // hand-editing it live are not a supported combination), and even if
    // they raced, each individual atomic store is still well-defined - just
    // documented here because "which thread writes this" is otherwise a safe
    // assumption everywhere else in this file.
    //
    // Defaults to C2 (see the constructor above), not the array's own 0.0f
    // zero-init - matches SynthPanel::noteNameForMidiNote's octave anchor
    // (MIDI 48 == "C3"), so C2 == MIDI 36. A freshly-cleared step used to
    // default to log2Hz(0.0f), a pitch far below C1 that only ever mattered
    // if the step's gate was hand-turned on without also setting its pitch -
    // housekeeping fix, documents/TODO.md.
    static constexpr int defaultStepMidiNote = 36; // C2

    std::array<std::atomic<float>, seqMaxSteps> stepPitchLog2Hz {};

    // 0 = rest, 1 = gate on. int, not bool: std::atomic<bool> is not
    // guaranteed lock-free on every platform, and every other discrete
    // switch in this struct (envelopeDestination, lfoWaveform, ...) already
    // uses int for exactly that reason. Same second-writer note as
    // stepPitchLog2Hz just above - build step 7 writes this one from the
    // audio thread too, in lockstep with it.
    std::array<std::atomic<int>, seqMaxSteps> stepGateOn {};

    // Realized as velocity into SynthVoice, not a separate depth path -
    // SynthVoice.cpp:183-188 and SynthVoice.h:82-86 already name this as
    // accent's landing point. Build step 3 wires it in; step 1 only owns the
    // storage.
    std::array<std::atomic<int>, seqMaxSteps> stepAccent {};

    // The arp's "Tie" idea, reused verbatim: skip force-closing the gate
    // across this step's boundary so SynthVoice takes its legato branch and
    // glides instead of re-triggering. See arpeggiator-design.md section 10;
    // build step 2 is what actually reads this.
    std::array<std::atomic<int>, seqMaxSteps> stepSlide {};

    // Additive term into SynthVoice's existing cutoffModulationOctaves sum,
    // via SynthVoice::setStepFilterModulation - StepSequencer::process calls
    // it once per step boundary, independent of gate state (build step 5).
    // 0 maps to exactly 0 octaves (inert, matching this array's own zero
    // default); 1 maps to +SynthVoice::seqCutoffModRangeOctaves, a one-
    // directional lift rather than a range centred on some midpoint, chosen
    // so an unedited lane never shifts the sound. Smoothed at the point of
    // use inside SynthVoice, never a raw snap, same as every other
    // continuous modulation depth here: a snapped value at 16th-note rates
    // would be far more audible than an occasional knob turn.
    std::array<std::atomic<float>, seqMaxSteps> stepCutoffNorm {};

    // Feeds SynthVoice's resonance modulation summing point - added straight
    // onto the resonance knob's own value, then clamped to [0,1] before
    // reaching Vcf::processSample (build step 5). Section 5's open question
    // is resolved this way: resonance is already a normalised 0..1 quantity,
    // so a simple additive offset was chosen over forcing cutoff's
    // octave-style shape onto it - the sequencer is the first-ever consumer
    // of resonance modulation in this instrument. Smoothed at the point of
    // use, same reasoning as stepCutoffNorm above - a resonance jump thumps
    // the whole feedback loop at once, same reason the knob itself ramps
    // slower than everything else (SynthVoice::resonanceRampSeconds).
    std::array<std::atomic<float>, seqMaxSteps> stepResonanceNorm {};

    // Discrete switch, raw per block - matches arpEnabled's treatment. Off on
    // first load, same reasoning: the instrument must not start sequencing
    // notes nobody programmed.
    std::atomic<int> seqEnabled { 0 };
    std::atomic<int> seqDivision { (int) StepDivision::Sixteenth };
    std::atomic<int> seqPatternLength { seqMaxSteps };

    // A FRACTION of the step, same reasoning and same clamp range as
    // arpGateLength.
    std::atomic<float> seqGateLength { 0.5f };

    // Item 7, build step 7 (pitch entry). Off on first load, same reasoning
    // as seqEnabled - arming record on startup would be a surprise. While
    // this is on AND the sequencer owns the voice (seqEnabled also on -
    // recording has no effect otherwise, since only StepSequencer::process
    // ever reads it, and that only runs while Seq is the voice owner), the
    // router's currently-held-note resolution (the same priority-resolved
    // pick Keys itself would sound - NoteStack::getCurrentResolution, sampled
    // once per block same as the arp's own liveNotes) is written into
    // stepPitchLog2Hz/stepGateOn at every step boundary, overwriting
    // whatever was stored there before it is read back for that same step's
    // playback. See documents/step-sequencer-design.md section 8 and
    // StepSequencer::process's own comment for the two build-time decisions
    // that document left open:
    //   - no key held at a boundary records a REST (gateOn = false), not a
    //     hold-over of the previous step's note;
    //   - a note held across several step boundaries is NOT inferred as a
    //     tie/slide - each boundary it is still down records as its own
    //     freshly-gated (retriggering) step, because recording only ever
    //     touches THIS field and stepGateOn, never stepSlide/stepAccent - the
    //     grid is still where slide gets set, by hand, same as accent.
    // Deliberately does NOT auto-disarm at the end of one pass through the
    // pattern - recording WRAPS, re-capturing over the top for as long as
    // this stays on, until the user turns it off. That is what "wrap" or
    // "auto-stop" resolves to: the natural behaviour of a plain armed flag
    // consulted every step boundary, with no extra state needed to get it.
    std::atomic<int> seqRecordArmed { 0 };

    // AUDIO -> UI. Written by StepSequencer::process (audio thread) at every
    // step boundary while the sequencer owns the voice; read by SynthPanel's
    // StepGrid (a UI-thread Timer) to highlight the currently playing step
    // (documents/step-sequencer-design.md section 9). -1 means "not
    // currently playing" - StepSequencer::releaseVoice sets it back there on
    // every hand-over away from the sequencer, so a stale highlight can
    // never survive the voice changing owners. Introduced in item 7 build
    // step 6 as "the one atomic in this whole struct that flows the OPPOSITE
    // direction" - build step 7 above adds stepPitchLog2Hz/stepGateOn as a
    // second, conditional case of the same direction, so this is no longer
    // literally the only one, just the unconditional one.
    std::atomic<int> currentStepForUi { -1 };

    //==============================================================================
    // Item 10 (character-and-vim.md) - v1 scope. See that doc's "V1 build
    // scope" section for exactly what each one gates and what is
    // deliberately deferred.

    // Tier 1: the single global "Vim" switch. Discrete, no smoother, same
    // treatment as every other on/off atomic here (arpEnabled, seqEnabled).
    // Off on first load - a clean/clinical voice is the safe default, and the
    // whole point of this switch is that it stays reachable for A/B. Gates
    // A2 (Adsr's exponential decay/release curves) only in v1 - see that
    // doc's scope-cut note for what else it will eventually gate.
    std::atomic<int> vimEnabled { 0 };

    // Tier 2 (A1 - drive/distortion). REVISED THREE TIMES - see
    // Source/DSP/Drive.h's own comment for the full history. Settled design:
    // a plain gain-into-clip distortion stage (Drive::processSample) applied
    // by SynthVoice AFTER the filter, just before the VCA - "a pedal at the
    // output", independent of cutoff/resonance position, rather than
    // "driving the filter's own maths" (the original, removed design) or
    // sitting pre-filter where the VCF could remove the very harmonics it
    // just added (the second, also-removed design). Knob-only, no separate
    // toggle - 0 default is an exact bypass, the same "0 = no effect"
    // convention every other depth knob here already uses
    // (envToCutoffDepthOctaves, lfoToPitchDepthOctaves, ...). Smoothed like
    // the other Tier 2/depth knobs - an unsmoothed jump into a nonlinear
    // shaper would click.
    std::atomic<float> driveAmount { 0.0f };

    // Tier 2 (B2 - humanise): ONE knob for v1, not Swing/Timing Jitter/
    // Velocity Jitter as three separate amounts - see the doc's scope-cut
    // note. 0 = fully quantised, today's exact unswung/unjittered behaviour.
    // Read raw once per block by Arpeggiator::process/StepSequencer::process,
    // same treatment as their own arpGateLength/seqGateLength - consumed once
    // per STEP, not per sample, so smoothing it would just lag the knob for
    // no benefit.
    std::atomic<float> humaniseAmount { 0.0f };

    // Tier 1-shaped (B1 - chorus): a plain on/off, not a depth knob - v1
    // ships fixed internal rate/depth constants (Chorus.h), no Rate/Depth
    // knobs yet. Off on first load, matching every other "instrument must
    // play normally out of the box" switch here.
    std::atomic<int> chorusEnabled { 0 };

    static_assert (std::atomic<float>::is_always_lock_free,
                   "Parameter stores must not take a lock on the message thread "
                   "or block the audio thread reading them.");
    static_assert (std::atomic<int>::is_always_lock_free,
                   "Same requirement as the float parameters above.");
};

// Message-thread only (comma/period's poll, Octave Up/Down clicks) - never
// called from the audio thread, so a plain load-then-store is safe: there is
// exactly one writer thread. Shared by QwertyNoteInput and SynthPanel so a
// key press and a button click move the exact same value the exact same way.
inline void adjustMasterOctaveShift (VoiceParameters& params, int delta) noexcept
{
    const auto next = params.masterOctaveShift.load (std::memory_order_relaxed) + delta;

    params.masterOctaveShift.store (
        next < VoiceParameters::minMasterOctaveShift ? VoiceParameters::minMasterOctaveShift
      : next > VoiceParameters::maxMasterOctaveShift ? VoiceParameters::maxMasterOctaveShift
      : next,
        std::memory_order_relaxed);
}
