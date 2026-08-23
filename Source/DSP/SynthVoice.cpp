#include "SynthVoice.h"

#include <cmath>

void SynthVoice::prepare (double newSampleRate)
{
    oscillator.prepare (newSampleRate);

    pitchLog2Smoothed.reset (newSampleRate, rampSeconds);
    sawLevelSmoothed.reset (newSampleRate, rampSeconds);
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

        const auto frame = oscillator.processSample();

        // Source mixer - one term for now. Pulse, sub and noise join at steps
        // 3 and 4, each with its own independent level.
        const auto mix = frame.saw * sawLevelSmoothed.getNextValue();

        output[i] = Vca::processSample (mix, outputLevelSmoothed.getNextValue(), amplitudeModulation);
    }
}
