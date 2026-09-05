#include "SynthVoice.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "CharacterProcessor.h"

void SynthVoice::prepare (double newSampleRate)
{
    oscillator.prepare (newSampleRate);
    filter.prepare (newSampleRate);
    envelope.prepare (newSampleRate);
    lfo.prepare (newSampleRate);
    glide.prepare (newSampleRate);
    mainDrift.prepare (newSampleRate);

    sawLevelSmoothed.reset (newSampleRate, rampSeconds);
    pulseLevelSmoothed.reset (newSampleRate, rampSeconds);
    pulseWidthSmoothed.reset (newSampleRate, rampSeconds);
    subLevelSmoothed.reset (newSampleRate, rampSeconds);
    noiseLevelSmoothed.reset (newSampleRate, rampSeconds);
    cutoffLog2Smoothed.reset (newSampleRate, rampSeconds);
    resonanceSmoothed.reset (newSampleRate, resonanceRampSeconds);
    outputLevelSmoothed.reset (newSampleRate, rampSeconds);
    sustainLevelSmoothed.reset (newSampleRate, rampSeconds);
    envToCutoffDepthSmoothed.reset (newSampleRate, rampSeconds);
    lfoToPitchDepthSmoothed.reset (newSampleRate, rampSeconds);
    lfoToCutoffDepthSmoothed.reset (newSampleRate, rampSeconds);
    velocityToAmpDepthSmoothed.reset (newSampleRate, rampSeconds);
    velocityToCutoffDepthSmoothed.reset (newSampleRate, rampSeconds);
    filterDriveAmountSmoothed.reset (newSampleRate, rampSeconds);

    // Not part of snapshotParameters' apply() list below - setStepFilterModulation
    // drives these directly - but still need their ramp step-size machinery
    // set up at the current sample rate. reset() does not disturb the stored
    // value, so these stay at their post-construction 0/0 until first pushed.
    stepCutoffNormSmoothed.reset (newSampleRate, rampSeconds);
    stepResonanceNormSmoothed.reset (newSampleRate, resonanceRampSeconds);

    snapshotParameters (true); // jump straight to target - block 1 shouldn't ramp up from zero
}

void SynthVoice::reset() noexcept
{
    oscillator.reset();
    noise.reset();
    filter.reset();
    envelope.reset();
    lfo.reset();
    mainDrift.reset();

    // Middle C, purely as a defined starting point - the first note-on snaps
    // away from it before anything is audible, since a fresh trigger never
    // glides.
    glide.reset (defaultPitchLog2Hz);

    voiceGated = false;
    currentVelocity = 0.0f;

    // A device stop must not leave a stale filter-lane modulation hanging
    // around, same hygiene reasoning as currentVelocity above - S8's
    // ordering (voice.reset(); router.reset(); arp.reset(); seq.reset();)
    // means whichever pattern was last pushed here should not survive a
    // restart.
    stepCutoffNormSmoothed.setCurrentAndTargetValue (0.0f);
    stepResonanceNormSmoothed.setCurrentAndTargetValue (0.0f);
}

//==============================================================================
void SynthVoice::noteOn (float pitchLog2Hz, float velocity) noexcept
{
    currentVelocity = velocity;

    if (! voiceGated)
    {
        // Fresh trigger from silence. Always restarts the envelope, and the
        // pitch SNAPS rather than sliding - a phrase's opening note should
        // not glide in from whatever pitch the ramp was left at, possibly
        // minutes ago.
        glide.setTarget (pitchLog2Hz);
        glide.snapToTarget();
        envelope.noteOn();
    }
    else
    {
        // Overlapping note-on. Pitch always ramps here - that is what makes a
        // glide audible at all - and the mode decides the envelope.
        glide.setTarget (pitchLog2Hz);

        const auto mode = (LegatoRetriggerMode) parameters.legatoRetriggerMode.load (std::memory_order_relaxed);

        if (mode == LegatoRetriggerMode::Retrigger)
            envelope.noteOn();
    }

    voiceGated = true;
}

void SynthVoice::retargetPitch (float pitchLog2Hz) noexcept
{
    // A key came up and revealed another still held. Never a retrigger, in
    // EITHER mode, because nothing was newly pressed - which is precisely why
    // this is a separate method rather than a flag on noteOn().
    glide.setTarget (pitchLog2Hz);
}

void SynthVoice::noteOff() noexcept
{
    envelope.noteOff();
    voiceGated = false;
}

void SynthVoice::setStepFilterModulation (float cutoffNorm, float resonanceNorm) noexcept
{
    // Pushed straight into each smoother's target - see the doc comment in
    // SynthVoice.h for why this bypasses the atomic-polling apply() pattern.
    stepCutoffNormSmoothed.setTargetValue (cutoffNorm);
    stepResonanceNormSmoothed.setTargetValue (resonanceNorm);
}

void SynthVoice::snapshotParameters (bool jumpImmediately) noexcept
{
    // Change-guard: renderNextBlock calls this once per block normally, but
    // 2-3 times when the arp lands more than one step in a block (see
    // documents/arpeggiator-design.md section 3). Whether repeatedly calling
    // SmoothedValue::setTargetValue with an UNCHANGED target restarts its ramp
    // was flagged unconfirmed in arpeggiator-design.md section 13 point 1 -
    // this guard makes the answer not matter, by simply not calling it again
    // when the value has not moved. Costs one float comparison per parameter;
    // the jump path (prepare() only, once) always applies and refreshes the
    // cache regardless, so it stays correct either way.
    const auto apply = [jumpImmediately] (Smoothed& smoother, float& lastStored, float value)
    {
        if (jumpImmediately)
        {
            smoother.setCurrentAndTargetValue (value);
            lastStored = value;
        }
        else if (value != lastStored)
        {
            smoother.setTargetValue (value);
            lastStored = value;
        }
    };

    apply (sawLevelSmoothed,    lastSawLevel,    parameters.sawLevel    .load (std::memory_order_relaxed));
    apply (pulseLevelSmoothed,  lastPulseLevel,  parameters.pulseLevel  .load (std::memory_order_relaxed));
    apply (pulseWidthSmoothed,  lastPulseWidth,  parameters.pulseWidth  .load (std::memory_order_relaxed));
    apply (subLevelSmoothed,    lastSubLevel,    parameters.subLevel    .load (std::memory_order_relaxed));
    apply (noiseLevelSmoothed,  lastNoiseLevel,  parameters.noiseLevel  .load (std::memory_order_relaxed));
    apply (cutoffLog2Smoothed,  lastCutoffLog2,  parameters.cutoffLog2Hz.load (std::memory_order_relaxed));
    apply (resonanceSmoothed,   lastResonance,   parameters.resonance   .load (std::memory_order_relaxed));
    apply (outputLevelSmoothed, lastOutputLevel, parameters.outputLevel .load (std::memory_order_relaxed));
    apply (sustainLevelSmoothed, lastSustainLevel, parameters.sustainLevel.load (std::memory_order_relaxed));
    apply (envToCutoffDepthSmoothed, lastEnvToCutoffDepth, parameters.envToCutoffDepthOctaves.load (std::memory_order_relaxed));
    apply (lfoToPitchDepthSmoothed, lastLfoToPitchDepth, parameters.lfoToPitchDepthOctaves.load (std::memory_order_relaxed));
    apply (lfoToCutoffDepthSmoothed, lastLfoToCutoffDepth, parameters.lfoToCutoffDepthOctaves.load (std::memory_order_relaxed));
    apply (velocityToAmpDepthSmoothed, lastVelocityToAmpDepth, parameters.velocityToAmpDepth.load (std::memory_order_relaxed));
    apply (velocityToCutoffDepthSmoothed, lastVelocityToCutoffDepth, parameters.velocityToCutoffDepthOctaves.load (std::memory_order_relaxed));
    apply (filterDriveAmountSmoothed, lastFilterDriveAmount, parameters.filterDriveAmount.load (std::memory_order_relaxed));
}

void SynthVoice::renderNextBlock (float* output, int numSamples) noexcept
{
    // Atomics are snapshotted once per block and the smoothers stepped once
    // per sample. Reading the atomics per sample would let a knob step
    // mid-block and click; reading per block without smoothing would step at
    // every block boundary.
    snapshotParameters (false);

    // Note events are NOT handled here any more - NoteRouter drains its FIFOs
    // and calls noteOn/retargetPitch/noteOff before this runs, so by the time
    // we get here the envelope and glide target are already set for this
    // block. See documents/note-handling-design.md section 7.

    // Envelope times are read once per block, not smoothed - see
    // documents/envelope-lfo-design.md section 5: changing one only affects
    // the rate of future samples, not the current output value, so there's no
    // click to smooth away. Sustain level IS smoothed (sustainLevelSmoothed),
    // because it's directly assigned as the output level during Sustain - fed
    // to the envelope once per sample, inside the loop below.
    envelope.setAttackSeconds  (parameters.attackSeconds .load (std::memory_order_relaxed));
    envelope.setDecaySeconds   (parameters.decaySeconds  .load (std::memory_order_relaxed));
    envelope.setReleaseSeconds (parameters.releaseSeconds.load (std::memory_order_relaxed));

    // character-and-vim.md item 10, Tier 1 "Vim". Discrete switch, raw per
    // block - same treatment as envelopeDestination/lfoWaveform below.
    // Shared by every Tier 1 mechanism this class gates (A2's envelope
    // curves, A3's main-oscillator and sub-oscillator drift, A6's velocity
    // curve) - one atomic, one read, matching the doc's own "one switch, no
    // sub-parameters" design.
    const auto vimEnabled = parameters.vimEnabled.load (std::memory_order_relaxed) != 0;

    // A2. Off (the default) hits the ADSR's original linear branch untouched.
    envelope.setCurveEnabled (vimEnabled);

    // A3. Off (the default) makes the sub-oscillator's own drift always
    // exactly 0.0f - see PolyBlepOscillator::setDriftEnabled's own comment.
    oscillator.setDriftEnabled (vimEnabled);

    // A6. currentVelocity is constant across a whole block (set once at
    // noteOn, never mid-block - note events are drained before this runs),
    // so the curve only needs computing once here rather than per sample.
    // Off (the default) is currentVelocity itself, untouched - see
    // curvedVelocity's own comment for why the curve is a no-op nowhere
    // except at the identity points regardless.
    const auto effectiveVelocity = vimEnabled ? curvedVelocity (currentVelocity) : currentVelocity;

    // Envelope destination is a discrete switch, read once per block like the
    // times above - see documents/envelope-lfo-design.md section 5.
    const auto envDestination = (EnvelopeDestination) parameters.envelopeDestination.load (std::memory_order_relaxed);
    const auto routeEnvToFilter = envDestination == EnvelopeDestination::Filter || envDestination == EnvelopeDestination::Both;
    const auto routeEnvToAmp    = envDestination == EnvelopeDestination::Amp    || envDestination == EnvelopeDestination::Both;

    // LFO waveform and rate: discrete switch and time constant respectively,
    // both read once per block like everything else in this category - see
    // documents/envelope-lfo-design.md section 5.
    lfo.setWaveform ((Lfo::Waveform) parameters.lfoWaveform.load (std::memory_order_relaxed));

    // Tempo sync (documents/tempo-sync-design.md section 3): reuses
    // StepClock's own beatsPerStepForDivision table rather than a second
    // Hz-from-BPM-and-division conversion - no new StepClock instance
    // needed, since that function is a free constexpr. Read raw once per
    // block in both branches, unsmoothed, same "time constant" category as
    // lfoRateHz on its own.
    const auto effectiveHz = parameters.lfoSyncEnabled.load (std::memory_order_relaxed) != 0
        ? (float) (1.0 / ((60.0 / (double) parameters.masterTempoBpm.load (std::memory_order_relaxed))
                           * beatsPerStepForDivision ((StepDivision) parameters.lfoSyncDivision.load (std::memory_order_relaxed))))
        : parameters.lfoRateHz.load (std::memory_order_relaxed);

    lfo.setRate (effectiveHz);

    // Also a time constant, read raw once per block - a change here alters
    // only the rate of an in-progress ramp, never its current position, so
    // there is nothing to smooth.
    glide.setGlideTimeSeconds (parameters.glideTimeSeconds.load (std::memory_order_relaxed));

    for (int i = 0; i < numSamples; ++i)
    {
        envelope.setSustainLevel (sustainLevelSmoothed.getNextValue());

        // Envelope value, 0..1. Called exactly once per sample here and
        // reused for both destinations below - pulling two different samples
        // of it in two places would desync envelope and audio.
        const auto envValue = envelope.processSample();

        // LFO value, bipolar -1..1. Also called exactly once per sample and
        // reused for both destinations below, for the same reason as envValue.
        const auto lfoValue = lfo.processSample();

        // Velocity -> amp summing point, item 7 build step 3. Multiplicative
        // and unity-defaulted at depth 0, same convention as the envelope
        // term it multiplies against below - a patch that never touches this
        // knob renders byte-identically to before this step, whatever
        // velocity happens to arrive.
        const auto velocityAmpFactor = 1.0f - velocityToAmpDepthSmoothed.getNextValue() * (1.0f - effectiveVelocity);

        // Amplitude modulation summing point - the shared ADSR (when routed
        // to the VCA) and item 7's accent multiply in here. Multiplicative
        // and unity-defaulted when not routed here, unlike the
        // additive-octaves pitch/cutoff points, which are additive and
        // zero-defaulted.
        const auto amplitudeModulation = (routeEnvToAmp ? envValue : 1.0f) * velocityAmpFactor;

        // character-and-vim.md A3: the main oscillator's own drift, in
        // octaves - see mainDrift's own comment in SynthVoice.h. Called every
        // sample regardless of vimEnabled (harmless - same "always advance,
        // zero contribution when off" posture as PolyBlepOscillator's own
        // sub-drift and the arp/seq's humanise generators), so the term is
        // EXACTLY 0.0f when off (0.0f * anything == 0.0f), not merely small.
        const auto rawMainDrift = mainDrift.processSample();
        const auto mainDriftOctaves = vimEnabled ? rawMainDrift * maxMainDriftOctaves : 0.0f;

        // Pitch modulation summing point, in octaves. Item 4's glide will add
        // its offset here too. Octaves rather than Hz so modulators compose
        // musically at any pitch.
        const auto pitchModulationOctaves = lfoValue * lfoToPitchDepthSmoothed.getNextValue() + mainDriftOctaves;

        const auto pitchOctaves = glide.processSample() + pitchModulationOctaves;
        oscillator.setFrequency (std::exp2 (pitchOctaves));
        oscillator.setPulseWidth (pulseWidthSmoothed.getNextValue());

        const auto frame = oscillator.processSample();

        // Source mixer - an independent level per source, matching the
        // SH-101's four mixer sliders. Noise is the one source that is not a
        // tap off the oscillator phase.
        const auto mix = frame.saw           * sawLevelSmoothed.getNextValue()
                       + frame.pulse         * pulseLevelSmoothed.getNextValue()
                       + frame.sub           * subLevelSmoothed.getNextValue()
                       + noise.processSample() * noiseLevelSmoothed.getNextValue();

        // Cutoff modulation summing point, in octaves - FOUR sources land
        // here: the shared envelope, the LFO, velocity (item 7 build step 3)
        // and now the step sequencer's own filter lane (build step 5).
        // Octaves rather than Hz because a modulator that moves the cutoff by
        // a fixed number of Hz sounds completely different at 200 Hz and at
        // 5 kHz. The exp2 happens inside Vcf, after this sum.
        // Velocity's own term: reference point is velocity == 1.0 (zero
        // shift there, matching every note source that doesn't vary velocity
        // - QWERTY, the arp, an accented sequencer step) - only a velocity
        // BELOW that pulls the cutoff down.
        // stepCutoffNormSmoothed's term: 0..1, scaled by the fixed
        // seqCutoffModRangeOctaves constant (SynthVoice.h) - 0 is exactly 0
        // octaves, matching the pattern array's own zero default.
        // All four terms are additive and zero-defaulted.
        const auto cutoffModulationOctaves =
            (routeEnvToFilter ? envValue * envToCutoffDepthSmoothed.getNextValue() : 0.0f)
            + lfoValue * lfoToCutoffDepthSmoothed.getNextValue()
            + (effectiveVelocity - 1.0f) * velocityToCutoffDepthSmoothed.getNextValue()
            + stepCutoffNormSmoothed.getNextValue() * seqCutoffModRangeOctaves;

        const auto cutoffOctaves = cutoffLog2Smoothed.getNextValue() + cutoffModulationOctaves;

        // Resonance modulation summing point, item 7 build step 5 - the step
        // sequencer's filter lane is the first-ever consumer of resonance
        // modulation in this instrument (documents/step-sequencer-design.md
        // section 5 confirms zero pre-existing env/LFO routing here). Unlike
        // cutoff's octave-style sum, resonance is already a normalised 0..1
        // quantity, so section 5's open question is resolved with a simple
        // additive offset instead - added straight onto the knob's own
        // value, zero-defaulted the same way as every other term above.
        // Clamped post-sum: not just taste but stability -
        // Vcf::processSample's feedback solution assumes resonance01 in
        // [0,1], and an out-of-range value (over-boosted, or pulled negative
        // by a future bipolar use) could push its denominator below 1.
        const auto resonance01 = juce::jlimit (0.0f, 1.0f,
            resonanceSmoothed.getNextValue() + stepResonanceNormSmoothed.getNextValue());

        const auto filtered = filter.processSample (mix, cutoffOctaves, resonance01,
                                                     filterDriveAmountSmoothed.getNextValue());

        output[i] = Vca::processSample (filtered, outputLevelSmoothed.getNextValue(), amplitudeModulation);
    }

    // Safety net. A single non-finite sample poisons the filter's integrator
    // states permanently - every later sample is NaN, the synth goes silent,
    // and no control can bring it back short of restarting the app. That is
    // an unacceptable failure mode for something meant to be played live, so
    // recover rather than merely assert: clear the block and reset the state.
    //
    // This should never fire now that the filter's feedback path is bounded.
    // If it does, that is a real bug worth chasing, not something to live
    // with - hence the assert alongside the recovery.
    auto blockIsFinite = true;

   #if JUCE_DEBUG
    // Debug-only sweep-testing aid, distinct from the recovery below: catches
    // a "technically finite but absurdly loud" bug - e.g. a mixing error -
    // that finiteness alone would not. Never alters the signal; it is purely
    // an assert, so this does nothing in Release. Sweep every knob to both
    // extremes with a Debug build and this either fires or it does not -
    // that is an objective pass/fail, not a listening judgement.
    static constexpr float maxPlausibleAmplitude = 32.0f;
    auto peakAbsSample = 0.0f;
   #endif

    for (int i = 0; i < numSamples; ++i)
    {
        if (! std::isfinite (output[i]))
        {
            blockIsFinite = false;
            break;
        }

       #if JUCE_DEBUG
        peakAbsSample = juce::jmax (peakAbsSample, std::abs (output[i]));
       #endif
    }

   #if JUCE_DEBUG
    jassert (peakAbsSample < maxPlausibleAmplitude);
   #endif

    if (! blockIsFinite)
    {
        jassertfalse;

        filter.reset();
        std::fill (output, output + numSamples, 0.0f);
    }
}

//==============================================================================
#if JUCE_DEBUG

namespace
{
    constexpr double selfTestSampleRate = 48000.0;
    constexpr int selfTestBlockSamples = 1000;

    // log2(220) - A3. Arbitrary mid-range pitch, same "defined value, never
    // tuned for anything" spirit as defaultPitchLog2Hz above; picked only so
    // the saw source has harmonic content for a cutoff shift to act on.
    constexpr float selfTestPitchLog2Hz = 7.7814f;

    float peakAbs (const float* buffer, int numSamples) noexcept
    {
        auto peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (buffer[i]));
        return peak;
    }

    // One voice, one note, one block. Destination pinned to Filter rather
    // than left at its own default (Amp) so the shared ADSR's attack ramp
    // never touches the amp path under test - amplitudeModulation then comes
    // ENTIRELY from velocityAmpFactor, with nothing else moving during the
    // block. Every depth this test isn't specifically exercising is passed
    // as 0, matching the atomics' own real defaults.
    void renderOneNote (float velocity, float velocityToAmpDepth, float velocityToCutoffDepthOctaves,
                         float* output, int numSamples)
    {
        SynthVoice voice;
        auto& parameters = voice.getParameters();
        parameters.envelopeDestination.store ((int) EnvelopeDestination::Filter);
        parameters.velocityToAmpDepth.store (velocityToAmpDepth);
        parameters.velocityToCutoffDepthOctaves.store (velocityToCutoffDepthOctaves);

        voice.prepare (selfTestSampleRate); // snapshots the atomics above, jumping straight to target
        voice.noteOn (selfTestPitchLog2Hz, velocity);
        voice.renderNextBlock (output, numSamples);
    }

    // Item 10's own rig: same shape as renderOneNote, but adds vimEnabled -
    // exercises A3 (main + sub oscillator drift, both read through
    // vimEnabled once per block) and A6 (the velocity curve) together, the
    // same "one switch, all-or-nothing" way the real panel's VIM checkbox
    // does. saw+sub both non-zero so a sub-oscillator-only bug (A3's own
    // split between SynthVoice and PolyBlepOscillator) couldn't hide behind
    // an unused sub level.
    void renderOneNoteWithVim (float velocity, bool vimEnabled, float velocityToAmpDepth,
                                float* output, int numSamples)
    {
        SynthVoice voice;
        auto& parameters = voice.getParameters();
        parameters.envelopeDestination.store ((int) EnvelopeDestination::Filter);
        parameters.velocityToAmpDepth.store (velocityToAmpDepth);
        parameters.vimEnabled.store (vimEnabled ? 1 : 0);
        parameters.subLevel.store (0.5f);

        voice.prepare (selfTestSampleRate);
        voice.noteOn (selfTestPitchLog2Hz, velocity);
        voice.renderNextBlock (output, numSamples);
    }

    // Item 7 build step 5. Same shape as renderOneNote above, but pushes the
    // per-step filter-lane values via setStepFilterModulation instead of an
    // atomic - there is no atomic to set, since these are audio-thread
    // working state, not a VoiceParameters knob. Velocity is fixed at 1.0 and
    // both velocity depths stay at their real 0 default, so build step 3's
    // terms cannot be the source of any difference this test observes.
    void renderOneNoteWithStepMod (float stepCutoffNorm, float stepResonanceNorm,
                                    float* output, int numSamples)
    {
        SynthVoice voice;
        auto& parameters = voice.getParameters();
        parameters.envelopeDestination.store ((int) EnvelopeDestination::Filter);

        voice.prepare (selfTestSampleRate);
        voice.setStepFilterModulation (stepCutoffNorm, stepResonanceNorm);
        voice.noteOn (selfTestPitchLog2Hz, 1.0f);
        voice.renderNextBlock (output, numSamples);
    }
}

void runAccentDepthSelfTest()
{
    using Buffer = std::array<float, selfTestBlockSamples>;

    //==========================================================================
    // BOTH DEPTHS INERT AT THEIR REAL DEFAULT (0): a full-velocity and a
    // half-velocity note must render byte-identically - velocityAmpFactor's
    // "1.0f - 0.0f * (1.0f - v)" degrades to exactly 1.0f regardless of v,
    // and the cutoff term's "(v - 1.0f) * 0.0f" degrades to exactly 0.0f the
    // same way. This is documents/step-sequencer-design.md section 12's
    // "at default, byte-identical" claim, proven rather than assumed.
    Buffer fullVelocityBothZero {};
    Buffer halfVelocityBothZero {};
    renderOneNote (1.0f, 0.0f, 0.0f, fullVelocityBothZero.data(), selfTestBlockSamples);
    renderOneNote (0.5f, 0.0f, 0.0f, halfVelocityBothZero.data(), selfTestBlockSamples);
    jassert (fullVelocityBothZero == halfVelocityBothZero);

    //==========================================================================
    // VELOCITY -> AMP, TURNED UP: isolated by holding the cutoff depth at 0.
    // At depth 1.0 the factor is exactly velocity itself (1 - 1*(1-v) = v),
    // so the two renders are the SAME filtered signal scaled by an exact,
    // known constant - not just "quieter", but exactly half, since Vca is a
    // bare multiply (Vca.h) and 0.5 is exactly representable.
    Buffer fullVelocityAmpDepth {};
    Buffer halfVelocityAmpDepth {};
    renderOneNote (1.0f, 1.0f, 0.0f, fullVelocityAmpDepth.data(), selfTestBlockSamples);
    renderOneNote (0.5f, 1.0f, 0.0f, halfVelocityAmpDepth.data(), selfTestBlockSamples);
    const auto peakFullAmp = peakAbs (fullVelocityAmpDepth.data(), selfTestBlockSamples);
    const auto peakHalfAmp = peakAbs (halfVelocityAmpDepth.data(), selfTestBlockSamples);
    jassert (peakFullAmp > 0.01f); // sanity - the reference note must actually be sounding
    jassert (peakHalfAmp == peakFullAmp * 0.5f);

    //==========================================================================
    // VELOCITY -> CUTOFF, TURNED UP: isolated by holding the amp depth at 0,
    // so amplitudeModulation is identically 1.0f in both renders and cannot
    // be the source of any difference. Reference velocity (1.0) contributes
    // a zero shift by construction; half velocity pulls the cutoff down a
    // full octave at this depth, which the default 2000 Hz cutoff and the
    // saw's harmonic content are comfortably positioned to make audible.
    // No exact ratio to check here - unlike Vca's bare multiply, Vcf's
    // response to a cutoff shift isn't a closed form worth hand-deriving in
    // a test - so this only proves the term is REACHING the filter, same
    // "not byte-identical" idiom runStepSequencerRenderSelfTest already uses
    // for its own cutoff/resonance-lane and accent checks.
    Buffer fullVelocityCutoffDepth {};
    Buffer halfVelocityCutoffDepth {};
    renderOneNote (1.0f, 0.0f, 2.0f, fullVelocityCutoffDepth.data(), selfTestBlockSamples);
    renderOneNote (0.5f, 0.0f, 2.0f, halfVelocityCutoffDepth.data(), selfTestBlockSamples);
    jassert (! (fullVelocityCutoffDepth == halfVelocityCutoffDepth));
}

void runFilterAutomationSelfTest()
{
    using Buffer = std::array<float, selfTestBlockSamples>;

    //==========================================================================
    // UNTOUCHED VS. EXPLICITLY (0, 0): there is no separate depth knob here -
    // the lane VALUE is what gets summed directly - so the only inert case is
    // the value itself being 0, exactly matching stepCutoffNorm/
    // stepResonanceNorm's own zero default. Exact, not approximate:
    // 0 * seqCutoffModRangeOctaves is exactly 0.0f, and
    // resonanceSmoothed + 0.0f is exactly resonanceSmoothed - so a note that
    // never calls setStepFilterModulation renders byte-identically to one
    // that calls it with both lanes at 0.
    Buffer untouched {};
    Buffer explicitZero {};
    renderOneNote (1.0f, 0.0f, 0.0f, untouched.data(), selfTestBlockSamples);
    renderOneNoteWithStepMod (0.0f, 0.0f, explicitZero.data(), selfTestBlockSamples);
    jassert (untouched == explicitZero);

    //==========================================================================
    // CUTOFF LANE, TURNED UP: isolated by holding the resonance lane at 0.
    // No exact ratio to check - same "not byte-identical" idiom
    // runAccentDepthSelfTest's own cutoff-depth block uses, for the same
    // reason: Vcf's response to a cutoff shift isn't a closed form worth
    // hand-deriving in a test.
    Buffer cutoffBaseline {};
    Buffer cutoffLifted {};
    renderOneNoteWithStepMod (0.0f, 0.0f, cutoffBaseline.data(), selfTestBlockSamples);
    renderOneNoteWithStepMod (1.0f, 0.0f, cutoffLifted.data(), selfTestBlockSamples);
    jassert (peakAbs (cutoffBaseline.data(), selfTestBlockSamples) > 0.01f); // sanity - actually sounding
    jassert (! (cutoffBaseline == cutoffLifted));

    //==========================================================================
    // RESONANCE LANE, TURNED UP: isolated by holding the cutoff lane at 0.
    // Same idiom, isolating the brand-new resonance summing point this build
    // step adds - the first modulation input resonance has ever had in this
    // instrument.
    Buffer resonanceBaseline {};
    Buffer resonanceLifted {};
    renderOneNoteWithStepMod (0.0f, 0.0f, resonanceBaseline.data(), selfTestBlockSamples);
    renderOneNoteWithStepMod (0.0f, 1.0f, resonanceLifted.data(), selfTestBlockSamples);
    jassert (! (resonanceBaseline == resonanceLifted));
}

void runLfoTempoSyncSelfTest()
{
    // LFO -> cutoff, Square wave, wide depth. Base cutoff (10.0f log2Hz =
    // ~1kHz) plus/minus 6 octaves clamps to Vcf's own [20, 18000] Hz range
    // (Vcf.h) at BOTH ends - 65536 Hz clamps to wide-open, 16 Hz clamps to
    // the floor, well below the ~220 Hz test pitch's fundamental - so the
    // two LFO phases land on a reliable clamp rather than depending on exact
    // filter-response numbers. Envelope pinned to Amp with a near-instant
    // ADSR (same shape as every transition rig in this codebase) so the
    // amplitude envelope settles to a near-constant sustain almost
    // immediately, leaving the cutoff-driven brightness swing as the only
    // thing moving peak amplitude across the render.
    const auto renderLfoSyncCutoff = [] (bool syncEnabled, float masterTempoBpm, StepDivision syncDivision,
                                          float freeRunHz, float* output, int numSamples)
    {
        SynthVoice voice;
        auto& parameters = voice.getParameters();
        parameters.envelopeDestination.store ((int) EnvelopeDestination::Amp);
        parameters.attackSeconds.store (0.001f);
        parameters.decaySeconds.store (0.001f);
        parameters.sustainLevel.store (0.8f);
        parameters.releaseSeconds.store (0.002f);

        parameters.cutoffLog2Hz.store (10.0f);
        parameters.resonance.store (0.1f);
        parameters.outputLevel.store (0.5f);
        parameters.sawLevel.store (0.7f);

        parameters.lfoWaveform.store ((int) Lfo::Waveform::Square);
        parameters.lfoToCutoffDepthOctaves.store (6.0f);
        parameters.lfoRateHz.store (freeRunHz);
        parameters.lfoSyncEnabled.store (syncEnabled ? 1 : 0);
        parameters.lfoSyncDivision.store ((int) syncDivision);
        parameters.masterTempoBpm.store (masterTempoBpm);

        voice.prepare (selfTestSampleRate); // jumps every atomic above straight to target
        voice.noteOn (selfTestPitchLog2Hz, 1.0f);
        voice.renderNextBlock (output, numSamples);
    };

    //==========================================================================
    // SYNC OFF, INERT PATH: masterTempoBpm/lfoSyncDivision must have ZERO
    // effect while lfoSyncEnabled is off - two renders that disagree wildly
    // on both must still be byte-identical.
    {
        constexpr int windowSamples = 2400;
        std::array<float, windowSamples> a {}, b {};
        renderLfoSyncCutoff (false, 120.0f, StepDivision::Sixteenth,   3.0f, a.data(), windowSamples);
        renderLfoSyncCutoff (false, 300.0f, StepDivision::ThirtySecond, 3.0f, b.data(), windowSamples);
        jassert (a == b);
    }

    //==========================================================================
    // SYNC ON: checks the exact half-period boundary the sync math predicts,
    // for two independent BPM/division pairs so this isn't just one lucky
    // number. Square's own phase (Lfo.cpp) starts at exactly +1 (loud/wide-
    // open) at phase 0 and flips to -1 (quiet/floor) at phase 0.5, and
    // Lfo::reset() (called from SynthVoice::prepare) zeroes phase - so
    // rendering starts precisely at the top of a cycle.
    const auto checkHalfPeriod = [&renderLfoSyncCutoff] (float masterTempoBpm, StepDivision division, int expectedHalfPeriodSamples)
    {
        constexpr int windowMargin = 700;  // settle time clear of both edges - see Vcf's cutoff floor time constant
        constexpr int windowLength = 200;

        std::array<float, 6000> output {}; // large enough for both test pairs' full period below
        const auto fullPeriod = 2 * expectedHalfPeriodSamples;
        jassert (fullPeriod <= (int) output.size());

        renderLfoSyncCutoff (true, masterTempoBpm, division, 3.0f, output.data(), fullPeriod);

        // Loud window: late in the first half, still clear of the boundary.
        const auto loudStart = expectedHalfPeriodSamples - windowMargin - windowLength;
        jassert (loudStart >= 0);
        const auto loudPeak = peakAbs (output.data() + loudStart, windowLength);

        // Quiet window: well into the second half, clear of the filter's own
        // settling time after the cutoff steps down to the floor.
        const auto quietStart = expectedHalfPeriodSamples + windowMargin;
        jassert (quietStart + windowLength <= fullPeriod);
        const auto quietPeak = peakAbs (output.data() + quietStart, windowLength);

        jassert (loudPeak > 0.01f); // sanity - the loud phase must actually be sounding
        jassert (quietPeak < loudPeak * 0.5f); // a wrong rate puts this window in the wrong phase
    };

    // 300 BPM / 1-16 -> 20 Hz -> 2400-sample period, 1200-sample half -
    // exact at this sample rate (48000 * 0.2 * 0.25 * 2 = 2400).
    checkHalfPeriod (300.0f, StepDivision::Sixteenth, 1200);

    // 240 BPM / 1-8 -> 8 Hz -> 6000-sample period, 3000-sample half - exact
    // (48000 * 0.25 * 0.5 * 2 = 6000), and independently confirms this isn't
    // one lucky BPM/division pair.
    checkHalfPeriod (240.0f, StepDivision::Eighth, 3000);
}

void runVimCharacterSelfTest()
{
    using Buffer = std::array<float, selfTestBlockSamples>;

    //==========================================================================
    // vimEnabled == false (the default): two independent renders, otherwise
    // identical settings, must agree exactly - A3's drift generators
    // producing no audible contribution when off, same as every other
    // "byte-identical at the inert default" proof in this file.
    {
        Buffer a {}, b {};
        renderOneNoteWithVim (0.7f, false, 0.5f, a.data(), selfTestBlockSamples);
        renderOneNoteWithVim (0.7f, false, 0.5f, b.data(), selfTestBlockSamples);

        jassert (peakAbs (a.data(), selfTestBlockSamples) > 0.01f); // sanity - actually sounding
        jassert (a == b);
    }

    //==========================================================================
    // vimEnabled == true: the render is no longer byte-identical to the off
    // case - A3's drift and/or A6's velocity curve are reaching the signal.
    // Velocity held below 1.0 with a real amp depth so A6's curve has
    // something to bite into even if A3's drift happened to roll a near-zero
    // sample this render.
    {
        Buffer off {}, on {};
        renderOneNoteWithVim (0.7f, false, 0.5f, off.data(), selfTestBlockSamples);
        renderOneNoteWithVim (0.7f, true,  0.5f, on.data(),  selfTestBlockSamples);

        jassert (! (off == on));
    }
}

#endif
