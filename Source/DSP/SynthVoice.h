#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Adsr.h"
#include "Glide.h"
#include "Lfo.h"
#include "NoiseGenerator.h"
#include "OscillatorDrift.h"
#include "PolyBlepOscillator.h"
#include "VoiceParameters.h"
#include "Vca.h"
#include "Vcf.h"

//==============================================================================
/*
    The voice core: no MIDI, no timers, no tempo, no juce::AudioBuffer — takes
    numbers, produces samples. This is the reuse boundary shared by the arp
    (item 5) and step sequencer (item 7) front ends — see
    documents/dsp-voice-design.md section 6.

    Item 2 (oscillator + filter core): four sources - saw, pulse/PWM, sub,
    noise - each with an independent level, mixed and sent through a 24dB
    resonant lowpass to the VCA. Built incrementally per
    documents/dsp-voice-design.md section 7.

    Item 3 (envelope + LFO): one shared ADSR, routable to VCF/VCA/Both
    (CLAUDE.md's hard constraint - one envelope, never a second), plus a
    triangle/square/sample-and-hold LFO routable to pitch and/or cutoff
    independently. All three modulation summing points item 2 left at zero
    are filled in. Built incrementally per
    documents/envelope-lfo-design.md section 7.

    Item 4 (note handling): pitch is no longer a UI parameter - it arrives as
    note events and is owned by Glide, with legato/retrigger deciding whether
    an overlapping note restarts the envelope. See
    documents/note-handling-design.md.

    Item 7, build step 3 (accent DSP): currentVelocity, captured but unused
    since item 4, gets two consumers - a multiplicative depth into the amp
    summing point and an additive-octaves depth into the existing cutoff
    summing point. Both default to 0 (inert). See
    documents/step-sequencer-design.md sections 1 and 5, and
    runAccentDepthSelfTest below.

    Item 7, build step 5 (filter automation lanes): setStepFilterModulation
    below is a NEW kind of input, called by StepSequencer at every step
    boundary rather than through VoiceParameters' atomic-polling pattern -
    the per-step filter-lane values are audio-thread-private working state,
    not a user-adjustable knob, same category as currentVelocity above.
    Feeds a fourth additive term into cutoffModulationOctaves and a brand-new
    resonance modulation summing point (documents/step-sequencer-design.md
    section 5's open question, resolved there: a simple additive 0..1
    offset, clamped post-sum, rather than forcing cutoff's octave-style
    shape onto an already-normalised quantity). See runFilterAutomationSelfTest
    below.
*/
class SynthVoice
{
public:
    SynthVoice() = default;

    void prepare (double newSampleRate);
    void reset() noexcept;

    // Writes into output, does not add to it. Raw-pointer + count rather than
    // an AudioSourceChannelInfo/AudioBuffer so item 5's sample-accurate arp
    // clock can call this several times per block with advancing pointers.
    void renderNextBlock (float* output, int numSamples) noexcept;

    VoiceParameters& getParameters() noexcept { return parameters; }

    //==============================================================================
    // Note control. AUDIO THREAD ONLY - NoteRouter calls these after draining
    // its event FIFOs at the top of a block. Never call them from the message
    // thread; that is what the FIFOs are for.
    //
    // Three methods, not two, and the third is load-bearing: noteOn() cannot
    // tell "a key went down" from "a key came up, revealing another still
    // held" by itself, and conflating those would make Retrigger mode
    // re-pluck on every note-off inside a chord. See
    // documents/note-handling-design.md section 5.

    // A key went down. Legato/retrigger mode decides whether this restarts
    // the envelope, but only when a note was already sounding.
    void noteOn (float pitchLog2Hz, float velocity) noexcept;

    // A key came up and another is still held. Pitch moves; the envelope is
    // never restarted, in either mode.
    void retargetPitch (float pitchLog2Hz) noexcept;

    // Every key is now up.
    void noteOff() noexcept;

    //==============================================================================
    // Filter automation from a per-step pattern (item 7 build step 5). Called
    // by StepSequencer at every step boundary, independent of gate state - a
    // rest can still sweep the filter (documents/step-sequencer-design.md
    // section 3). NOT a note-control method despite sitting next to them
    // above: it has no effect on pitch or the envelope, and can be called
    // whether the voice is gated, ungated, or between notes.
    //
    // Pushes straight into each value's own smoother's TARGET rather than
    // going through the atomic-polling apply()/lastXxx pattern every
    // VoiceParameters-backed member below uses - the caller already knows
    // exactly WHEN a value changes (a step boundary), so there is no
    // "unchanged value polled every block" case that pattern exists to guard
    // against.
    //
    // AUDIO THREAD.
    void setStepFilterModulation (float cutoffNorm, float resonanceNorm) noexcept;

private:
    void snapshotParameters (bool jumpImmediately) noexcept;

    // Audio-thread-private. Distinguishes "a note is already sounding" from
    // silence, which is the overlap signal legato/retrigger keys off.
    bool voiceGated = false;

    // Routed into the amp and cutoff summing points below as of item 7 build
    // step 3 (velocityToAmpDepth / velocityToCutoffDepthOctaves) - this also
    // substantially implements character-and-vim.md B5's velocity routing as
    // a side effect, per documents/step-sequencer-design.md's header notes.
    float currentVelocity = 0.0f;

    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    static constexpr double rampSeconds = 0.02;

    // A resonance jump moves the whole feedback loop at once and thumps, so
    // it gets a deliberately slower ramp than everything else.
    static constexpr double resonanceRampSeconds = 0.05;

    // Item 7 build step 5: fixed conversion from stepCutoffNorm (0..1, the
    // per-step filter-lane value setStepFilterModulation receives) into the
    // octave units cutoffModulationOctaves already sums in. 0 -> 0 octaves
    // (exactly inert, matching the pattern array's own zero default); 1 ->
    // +seqCutoffModRangeOctaves - a one-directional lift, not a range
    // centred on some midpoint, chosen so an unedited lane (0 everywhere)
    // never shifts the sound. UNVERIFIED BY EAR (CLAUDE.md's "what you
    // cannot verify") - a starting number, easy to retune once there is a UI
    // to feel it through (build step 6).
    static constexpr float seqCutoffModRangeOctaves = 4.0f;

    // character-and-vim.md A3. BY EAR, not derived, same posture as every
    // other shaping constant in this codebase - 2 cents is comfortably
    // inside the doc's own "sub-cent to a couple of cents" range. mainDrift
    // (above) produces a value nominally in roughly [-1, 1]; this scales it
    // into the octave domain the pitch summing point already sums in.
    static constexpr float maxMainDriftOctaves = 2.0f / 1200.0f;

    // log2(261.63) - middle C. Only a defined resting value for the glide
    // ramp before any note has ever played; the first note-on snaps away from
    // it, so it is never heard.
    static constexpr float defaultPitchLog2Hz = 8.0313f;

    VoiceParameters parameters;
    PolyBlepOscillator oscillator;
    NoiseGenerator noise;
    Vcf filter;
    Adsr envelope;
    Lfo lfo;

    // character-and-vim.md A3 (item 10). The MAIN oscillator's own half of
    // drift - an additive octave offset into the pitch summing point below,
    // same shape as every other pitch modulator here. The sub-oscillator's
    // own, independently-seeded half lives inside PolyBlepOscillator itself
    // (see that class's own comment for why it can't be expressed the same
    // way). Distinct seed from every other seeded generator in this
    // codebase.
    OscillatorDrift mainDrift { 0x3d5a91f6u };

    // Owns the base pitch outright - this REPLACED item 3's pitchLog2Smoothed
    // rather than layering on top of it. That smoother was a 20ms anti-zipper
    // ramp on a debug slider; glide IS the note-to-note pitch transition, so
    // modelling it as a second additive ramp would mean implementing the same
    // thing twice. See documents/note-handling-design.md section 4.
    Glide glide;

    // Linear smoothing on a log2(Hz) value IS multiplicative smoothing of the
    // frequency, which is the musically correct sweep - and it sidesteps
    // ValueSmoothingTypes::Multiplicative's strictly-positive constraint. One
    // smoother type everywhere.
    Smoothed sawLevelSmoothed;
    Smoothed pulseLevelSmoothed;
    Smoothed pulseWidthSmoothed;
    Smoothed subLevelSmoothed;
    Smoothed noiseLevelSmoothed;
    Smoothed cutoffLog2Smoothed;
    Smoothed resonanceSmoothed;
    Smoothed outputLevelSmoothed;
    Smoothed sustainLevelSmoothed;
    Smoothed envToCutoffDepthSmoothed;
    Smoothed lfoToPitchDepthSmoothed;
    Smoothed lfoToCutoffDepthSmoothed;
    Smoothed velocityToAmpDepthSmoothed;
    Smoothed velocityToCutoffDepthSmoothed;

    // character-and-vim.md A1 (item 10). Smoothed like every other depth
    // knob here - an unsmoothed jump would thump the filter's feedback loop
    // the same way an unsmoothed resonance jump does.
    Smoothed filterDriveAmountSmoothed;

    // Item 7 build step 5. NOT part of the lastXxx-guarded apply() list below
    // - setStepFilterModulation pushes these two directly via setTargetValue,
    // since the caller (StepSequencer) already knows exactly when a value
    // changes. resonanceRampSeconds for the resonance one, same thump
    // reasoning as the knob it is added onto.
    Smoothed stepCutoffNormSmoothed;
    Smoothed stepResonanceNormSmoothed;

    // The change-guard for snapshotParameters - see documents/arpeggiator-design.md
    // section 13 point 1. One cache per smoother, mirroring the list above.
    // Values are meaningless until the first prepare() call, which always runs
    // with jumpImmediately - so nothing reads a cache before it is written.
    float lastSawLevel = 0.0f;
    float lastPulseLevel = 0.0f;
    float lastPulseWidth = 0.0f;
    float lastSubLevel = 0.0f;
    float lastNoiseLevel = 0.0f;
    float lastCutoffLog2 = 0.0f;
    float lastResonance = 0.0f;
    float lastOutputLevel = 0.0f;
    float lastSustainLevel = 0.0f;
    float lastEnvToCutoffDepth = 0.0f;
    float lastLfoToPitchDepth = 0.0f;
    float lastLfoToCutoffDepth = 0.0f;
    float lastVelocityToAmpDepth = 0.0f;
    float lastVelocityToCutoffDepth = 0.0f;
    float lastFilterDriveAmount = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthVoice)
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    Covers what item 7 build step 3 actually adds: currentVelocity's two new
    summing points in SynthVoice::renderNextBlock. Same "output is the only
    thing that proves it" approach as the arp/sequencer self-tests, not a
    re-implementation of the formulas it checks.

    Proves, per depth knob, both halves of documents/step-sequencer-design.md
    section 12's claim - inert at default, live once turned up - since only
    checking one half would let either a dead wire or a permanently-on one
    pass unnoticed:

    - velocityToAmpDepth at 0: a full-velocity and a half-velocity note
      render byte-identically (the multiplicative factor degrades to exactly
      1 regardless of velocity). Turned up: the half-velocity note's peak
      amplitude is strictly quieter than the full-velocity note's.
    - velocityToCutoffDepthOctaves at 0: same byte-identical pairing (the
      additive term degrades to exactly 0). Turned up: the two notes' output
      is no longer byte-identical - the cutoff term is reaching the filter -
      checked with envelope/LFO cutoff modulation and the amp depth both held
      at 0 so nothing else could be the source of the difference.

    Whether the result actually sounds like "accent punches harder" is a
    listening judgement CLAUDE.md's "what you cannot verify" section reserves
    for the user - this only proves the wiring is live, not that the taste is
    right.
*/
void runAccentDepthSelfTest();

/*
    Debug-only self-test, run once at startup.

    Covers what item 7 build step 5 actually adds: setStepFilterModulation's
    two new summing points in SynthVoice::renderNextBlock. Same approach as
    runAccentDepthSelfTest above - output is the only thing that proves it -
    but there is no separate depth knob to hold at 0 here: the per-step lane
    VALUE is what is summed directly, so the only inert case is the lane
    value itself being 0 (matching VoiceParameters::stepCutoffNorm/
    stepResonanceNorm's own zero default).

    - Untouched vs. explicitly (0, 0): a note that never calls
      setStepFilterModulation renders byte-identically to one that calls it
      with both lanes at 0 - the mechanism, unused, does not perturb the
      sound. Exact: 0 * seqCutoffModRangeOctaves is exactly 0.0f, and
      resonanceSmoothed + 0.0f is exactly resonanceSmoothed.
    - Cutoff lane turned up (resonance held at 0): output is no longer
      byte-identical to the (0, 0) baseline - the term is reaching the
      filter. No exact ratio to check, same "not byte-identical" idiom
      runAccentDepthSelfTest's own cutoff-depth block uses, for the same
      reason: Vcf's response to a cutoff shift isn't a closed form worth
      hand-deriving in a test.
    - Resonance lane turned up (cutoff held at 0): same idiom, isolating the
      brand-new resonance summing point this build step adds.

    Whether either lane actually sounds like a musical filter sweep is a
    listening judgement CLAUDE.md's "what you cannot verify" section reserves
    for the user - this only proves the wiring is live.
*/
void runFilterAutomationSelfTest();

/*
    Debug-only self-test, run once at startup.

    Covers documents/tempo-sync-design.md section 3: the LFO-sync computation
    at the top of renderNextBlock. Output is the only thing that proves it -
    same approach as every other self-test in this file - not a
    re-implementation of the Hz-from-BPM-and-division formula it checks.

    - Sync off: masterTempoBpm/lfoSyncDivision must have ZERO effect - two
      renders with lfoSyncEnabled off but wildly different tempo/division
      values are byte-identical, proving the inert path is truly inert (same
      "prove it" style as runFilterAutomationSelfTest's untouched-vs-
      explicit-zero case above).
    - Sync on, at two known BPM/division pairs (300 BPM/1-16 -> 20 Hz; 240
      BPM/1-8 -> 8 Hz): LFO -> cutoff (Square wave, wide depth) turns the
      output into a loud/quiet square pattern whose half-period is
      arithmetically known in advance. A loud-phase window placed just before
      each expected half-period boundary and a quiet-phase window placed well
      after it (clear of the filter's own settling time) either land on the
      predicted phase or they don't - a wrong rate puts the quiet window back
      in the loud phase, or vice versa, so this catches a rate error the same
      way a wrong-length step would fail runStepClockSelfTest's exact sample
      arithmetic.

    Whether the synced LFO actually feels locked to the beat by ear is a
    listening judgement CLAUDE.md's "what you cannot verify" section reserves
    for the user - this only proves the arithmetic and the wiring.
*/
void runLfoTempoSyncSelfTest();

/*
    Debug-only self-test, run once at startup.

    Covers item 10's own wiring into SynthVoice specifically - not
    OscillatorDrift/CharacterProcessor/curvedVelocity in isolation (their own
    files' self-tests already do that), but that vimEnabled == false (the
    default) renders BYTE-IDENTICAL across independent renders (A3's drift
    generators produce no audible contribution when off, A6's velocity curve
    is the untouched value), and that vimEnabled == true actually changes the
    output - both mechanisms are reaching the real render path, not just
    correct in isolation.

    Whether the drift/curve actually sound like "instability"/"expressive
    velocity" is a listening judgement CLAUDE.md's "what you cannot verify"
    section reserves for the user - this only proves the wiring is live.
*/
void runVimCharacterSelfTest();

#endif
