#include "Lfo.h"

void Lfo::prepare (double newSampleRate) noexcept
{
    inverseSampleRate = 1.0 / newSampleRate;
    reset();
}

void Lfo::reset() noexcept
{
    phase = 0.0;

    // Reset the generator too (not just re-draw), so a fresh reset gives a
    // reproducible sequence - the same reason NoiseGenerator's seed exists.
    sampleAndHoldNoise.reset();
    heldRandomValue = sampleAndHoldNoise.processSample();
}

void Lfo::setRate (float rateHz) noexcept
{
    phaseIncrement = (double) rateHz * inverseSampleRate;
}

float Lfo::processSample() noexcept
{
    float value = 0.0f;

    switch (waveform)
    {
        case Waveform::Triangle:
        {
            // Symmetric ramp: -1 at phase 0, +1 at phase 0.5, back to -1 at
            // the wrap. Both halves agree at phase 0.5, so there's no
            // discontinuity at the peak.
            const auto t = (float) phase;
            value = t < 0.5f ? (-1.0f + 4.0f * t) : (3.0f - 4.0f * t);
            break;
        }

        case Waveform::Square:
            value = phase < 0.5 ? 1.0f : -1.0f;
            break;

        case Waveform::SampleAndHold:
            value = heldRandomValue;
            break;
    }

    phase += phaseIncrement;
    if (phase >= 1.0)
    {
        phase -= 1.0;

        // New value takes effect immediately at the first sample of the new
        // cycle - drawn here (after this sample's value was already read
        // above), held until the next wrap.
        if (waveform == Waveform::SampleAndHold)
            heldRandomValue = sampleAndHoldNoise.processSample();
    }

    return value;
}
