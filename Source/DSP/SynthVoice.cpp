#include "SynthVoice.h"

#include <cmath>

void SynthVoice::prepare (double newSampleRate)
{
    oscillator.prepare (newSampleRate);

    pitchLog2Smoothed.reset (newSampleRate, rampSeconds);
    sawLevelSmoothed.reset (newSampleRate, rampSeconds);
    pulseLevelSmoothed.reset (newSampleRate, rampSeconds);
    pulseWidthSmoothed.reset (newSampleRate, rampSeconds);
    outputLevelSmoothed.reset (newSampleRate, rampSeconds);

    snapshotParameters (true); // jump straight to target - block 1 shouldn't ramp up from zero
}

void SynthVoice::reset() noexcept
{
    oscillator.reset();
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

        // Source mixer - independent level per source, per the SH-101's four
        // mixer sliders. Sub and noise join at step 4.
        const auto mix = frame.saw   * sawLevelSmoothed.getNextValue()
                       + frame.pulse * pulseLevelSmoothed.getNextValue();

        output[i] = Vca::processSample (mix, outputLevelSmoothed.getNextValue(), amplitudeModulation);
    }
}
