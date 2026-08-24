#include "SynthVoice.h"

#include <algorithm>
#include <cmath>

void SynthVoice::prepare (double newSampleRate)
{
    oscillator.prepare (newSampleRate);
    filter.prepare (newSampleRate);
    envelope.prepare (newSampleRate);
    lfo.prepare (newSampleRate);
    glide.prepare (newSampleRate);

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

    snapshotParameters (true); // jump straight to target - block 1 shouldn't ramp up from zero
}

void SynthVoice::reset() noexcept
{
    oscillator.reset();
    noise.reset();
    filter.reset();
    envelope.reset();
    lfo.reset();

    // Middle C, purely as a defined starting point - the first note-on snaps
    // away from it before anything is audible, since a fresh trigger never
    // glides.
    glide.reset (defaultPitchLog2Hz);

    voiceGated = false;
    currentVelocity = 0.0f;
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

void SynthVoice::snapshotParameters (bool jumpImmediately) noexcept
{
    const auto apply = [jumpImmediately] (Smoothed& smoother, float value)
    {
        if (jumpImmediately)
            smoother.setCurrentAndTargetValue (value);
        else
            smoother.setTargetValue (value);
    };

    apply (sawLevelSmoothed,    parameters.sawLevel    .load (std::memory_order_relaxed));
    apply (pulseLevelSmoothed,  parameters.pulseLevel  .load (std::memory_order_relaxed));
    apply (pulseWidthSmoothed,  parameters.pulseWidth  .load (std::memory_order_relaxed));
    apply (subLevelSmoothed,    parameters.subLevel    .load (std::memory_order_relaxed));
    apply (noiseLevelSmoothed,  parameters.noiseLevel  .load (std::memory_order_relaxed));
    apply (cutoffLog2Smoothed,  parameters.cutoffLog2Hz.load (std::memory_order_relaxed));
    apply (resonanceSmoothed,   parameters.resonance   .load (std::memory_order_relaxed));
    apply (outputLevelSmoothed, parameters.outputLevel .load (std::memory_order_relaxed));
    apply (sustainLevelSmoothed, parameters.sustainLevel.load (std::memory_order_relaxed));
    apply (envToCutoffDepthSmoothed, parameters.envToCutoffDepthOctaves.load (std::memory_order_relaxed));
    apply (lfoToPitchDepthSmoothed, parameters.lfoToPitchDepthOctaves.load (std::memory_order_relaxed));
    apply (lfoToCutoffDepthSmoothed, parameters.lfoToCutoffDepthOctaves.load (std::memory_order_relaxed));
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

    // Envelope destination is a discrete switch, read once per block like the
    // times above - see documents/envelope-lfo-design.md section 5.
    const auto envDestination = (EnvelopeDestination) parameters.envelopeDestination.load (std::memory_order_relaxed);
    const auto routeEnvToFilter = envDestination == EnvelopeDestination::Filter || envDestination == EnvelopeDestination::Both;
    const auto routeEnvToAmp    = envDestination == EnvelopeDestination::Amp    || envDestination == EnvelopeDestination::Both;

    // LFO waveform and rate: discrete switch and time constant respectively,
    // both read once per block like everything else in this category - see
    // documents/envelope-lfo-design.md section 5.
    lfo.setWaveform ((Lfo::Waveform) parameters.lfoWaveform.load (std::memory_order_relaxed));
    lfo.setRate (parameters.lfoRateHz.load (std::memory_order_relaxed));

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

        // Amplitude modulation summing point - the shared ADSR (when routed
        // to the VCA) and item 7's accent multiply in here. Multiplicative
        // and unity-defaulted when not routed here, unlike the
        // additive-octaves pitch/cutoff points, which are additive and
        // zero-defaulted.
        const auto amplitudeModulation = routeEnvToAmp ? envValue : 1.0f;

        // Pitch modulation summing point, in octaves. Item 4's glide will add
        // its offset here too. Octaves rather than Hz so modulators compose
        // musically at any pitch.
        const auto pitchModulationOctaves = lfoValue * lfoToPitchDepthSmoothed.getNextValue();

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

        // Cutoff modulation summing point, in octaves - and unlike pitch, TWO
        // sources land here: the shared envelope and the LFO. Octaves rather
        // than Hz because a modulator that moves the cutoff by a fixed number
        // of Hz sounds completely different at 200 Hz and at 5 kHz. The exp2
        // happens inside Vcf, after this sum.
        const auto cutoffModulationOctaves =
            (routeEnvToFilter ? envValue * envToCutoffDepthSmoothed.getNextValue() : 0.0f)
            + lfoValue * lfoToCutoffDepthSmoothed.getNextValue();

        const auto cutoffOctaves = cutoffLog2Smoothed.getNextValue() + cutoffModulationOctaves;
        const auto filtered = filter.processSample (mix, cutoffOctaves, resonanceSmoothed.getNextValue());

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
