#include "SynthVoice.h"

#include <algorithm>
#include <cmath>

void SynthVoice::prepare (double newSampleRate)
{
    oscillator.prepare (newSampleRate);
    filter.prepare (newSampleRate);

    pitchLog2Smoothed.reset (newSampleRate, rampSeconds);
    sawLevelSmoothed.reset (newSampleRate, rampSeconds);
    pulseLevelSmoothed.reset (newSampleRate, rampSeconds);
    pulseWidthSmoothed.reset (newSampleRate, rampSeconds);
    subLevelSmoothed.reset (newSampleRate, rampSeconds);
    noiseLevelSmoothed.reset (newSampleRate, rampSeconds);
    cutoffLog2Smoothed.reset (newSampleRate, rampSeconds);
    resonanceSmoothed.reset (newSampleRate, resonanceRampSeconds);
    outputLevelSmoothed.reset (newSampleRate, rampSeconds);

    snapshotParameters (true); // jump straight to target - block 1 shouldn't ramp up from zero
}

void SynthVoice::reset() noexcept
{
    oscillator.reset();
    noise.reset();
    filter.reset();
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

    apply (pitchLog2Smoothed,   parameters.pitchLog2Hz .load (std::memory_order_relaxed));
    apply (sawLevelSmoothed,    parameters.sawLevel    .load (std::memory_order_relaxed));
    apply (pulseLevelSmoothed,  parameters.pulseLevel  .load (std::memory_order_relaxed));
    apply (pulseWidthSmoothed,  parameters.pulseWidth  .load (std::memory_order_relaxed));
    apply (subLevelSmoothed,    parameters.subLevel    .load (std::memory_order_relaxed));
    apply (noiseLevelSmoothed,  parameters.noiseLevel  .load (std::memory_order_relaxed));
    apply (cutoffLog2Smoothed,  parameters.cutoffLog2Hz.load (std::memory_order_relaxed));
    apply (resonanceSmoothed,   parameters.resonance   .load (std::memory_order_relaxed));
    apply (outputLevelSmoothed, parameters.outputLevel .load (std::memory_order_relaxed));
}

void SynthVoice::renderNextBlock (float* output, int numSamples) noexcept
{
    // Atomics are snapshotted once per block and the smoothers stepped once
    // per sample. Reading the atomics per sample would let a knob step
    // mid-block and click; reading per block without smoothing would step at
    // every block boundary.
    snapshotParameters (false);

    // Amplitude modulation summing point - item 3's shared ADSR (when routed
    // to the VCA) and item 7's accent multiply in here. Multiplicative and
    // unity-defaulted, unlike the additive-octaves pitch point below.
    const auto amplitudeModulation = 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Pitch modulation summing point, in octaves. Item 3's LFO -> pitch
        // adds here, and item 4's glide adds its offset here too. Octaves
        // rather than Hz so modulators compose musically at any pitch.
        const auto pitchModulationOctaves = 0.0f;

        const auto pitchOctaves = pitchLog2Smoothed.getNextValue() + pitchModulationOctaves;
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

        // Cutoff modulation summing point, in octaves - and unlike pitch,
        // TWO sources land here at item 3: the shared ADSR (env amount) and
        // the LFO. Octaves rather than Hz because a modulator that moves the
        // cutoff by a fixed number of Hz sounds completely different at
        // 200 Hz and at 5 kHz. The exp2 happens inside Vcf, after this sum.
        const auto cutoffModulationOctaves = 0.0f;

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
    for (int i = 0; i < numSamples; ++i)
    {
        if (! std::isfinite (output[i]))
        {
            jassertfalse;

            filter.reset();
            std::fill (output, output + numSamples, 0.0f);
            break;
        }
    }
}
