#include "SynthVoice.h"

void SynthVoice::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    phase = 0.0;
    phaseIncrement = testOscillatorFrequencyHz / sampleRate;

    outputLevelSmoothed.reset (sampleRate, rampSeconds);

    snapshotParameters (true); // jump straight to target - block 1 shouldn't ramp up from zero
}

void SynthVoice::reset() noexcept
{
    phase = 0.0;
}

void SynthVoice::snapshotParameters (bool jumpImmediately) noexcept
{
    const auto level = parameters.outputLevel.load (std::memory_order_relaxed);

    if (jumpImmediately)
        outputLevelSmoothed.setCurrentAndTargetValue (level);
    else
        outputLevelSmoothed.setTargetValue (level);
}

void SynthVoice::renderNextBlock (float* output, int numSamples) noexcept
{
    snapshotParameters (false);

    // Amplitude modulation summing point - item 3's shared ADSR (when routed
    // to the VCA) and item 7's accent multiply in here. Multiplicative and
    // unity-defaulted, unlike the additive-octaves pitch/cutoff points that
    // arrive with the real oscillator and filter.
    const auto amplitudeModulation = 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Naive (aliased) saw - deliberately not PolyBLEP yet. Replaced by
        // PolyBlepOscillator in item 2/step 2; kept here as the "before"
        // reference for that step's aliasing A/B.
        const auto naiveSaw = (float) (2.0 * phase - 1.0);

        phase += phaseIncrement;
        if (phase >= 1.0)
            phase -= 1.0;

        output[i] = Vca::processSample (naiveSaw, outputLevelSmoothed.getNextValue(), amplitudeModulation);
    }
}
