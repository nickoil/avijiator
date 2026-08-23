#include "PolyBlepOscillator.h"

#include <juce_core/juce_core.h>

void PolyBlepOscillator::prepare (double newSampleRate) noexcept
{
    inverseSampleRate = 1.0 / newSampleRate;
    reset();
}

void PolyBlepOscillator::reset() noexcept
{
    phase = 0.0;
}

void PolyBlepOscillator::setFrequency (float frequencyHz) noexcept
{
    phaseIncrement = juce::jmin (maxIncrement,
                                 (double) juce::jmax (minFrequencyHz, frequencyHz) * inverseSampleRate);
}

float PolyBlepOscillator::polyBlep (float t, float dt) noexcept
{
    if (t < dt)                             // the sample just after the edge
    {
        t /= dt;
        return t + t - t * t - 1.0f;        // 2t - t^2 - 1
    }

    if (t > 1.0f - dt)                      // the sample just before the edge
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;        // (t + 1)^2
    }

    return 0.0f;
}

PolyBlepOscillator::Frame PolyBlepOscillator::processSample() noexcept
{
    const auto t = (float) phase;
    const auto dt = (float) phaseIncrement;

    Frame frame;

    // Naive rising ramp, minus the correction for the single downward step of
    // amplitude 2 at the wrap. Without the correction this is the aliased saw
    // that step 1 produced.
    frame.saw = 2.0f * t - 1.0f - polyBlep (t, dt);

    // Phase advances once, after every tap has read it.
    phase += phaseIncrement;
    if (phase >= 1.0)
        phase -= 1.0;

    return frame;
}
