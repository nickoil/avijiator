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
    subHigh = false;
}

void PolyBlepOscillator::setFrequency (float frequencyHz) noexcept
{
    phaseIncrement = juce::jmin (maxIncrement,
                                 (double) juce::jmax (minFrequencyHz, frequencyHz) * inverseSampleRate);
}

void PolyBlepOscillator::setPulseWidth (float newPulseWidth) noexcept
{
    // Only the caller's intent is stored here; the dt-dependent clamp has to
    // happen in processSample, where the current increment is known.
    pulseWidth = newPulseWidth;
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

    // Each BLEP correction window is 2*dt wide, so if the two pulse edges get
    // closer together than that, their windows overlap and the corrections
    // corrupt each other. A static [0.02, 0.98] clamp is NOT enough at high
    // pitch - the clamp has to tighten with dt. Capping phaseIncrement at
    // fs/4 is what guarantees this lower limit never exceeds the upper one.
    // The graceful degradation is that very high notes are forced toward a
    // square, which is the right failure mode.
    const auto w = juce::jlimit (juce::jmax (minPulseWidth,         2.0f * dt),
                                 juce::jmin (maxPulseWidth, 1.0f - 2.0f * dt),
                                 pulseWidth);

    // A pulse has TWO discontinuities per cycle - a rising edge at 0 and a
    // falling edge at w - and PWM moves the second one. Each correction must
    // be evaluated in its own edge-relative phase.
    auto fallingPhase = t - w;
    if (fallingPhase < 0.0f)
        fallingPhase += 1.0f;

    frame.pulse = (t < w ? 1.0f : -1.0f)
                + polyBlep (t, dt)                  // rising edge at 0
                - polyBlep (fallingPhase, dt);      // falling edge at w

    // Sub-oscillator: a square one octave down, derived arithmetically from
    // the main phase plus the flip-flop bit - NOT run on its own accumulator.
    // A second accumulator incremented by dt*0.5 would accumulate rounding
    // error independently of the main one and slowly slip phase against the
    // saw, audible as slow beating over tens of seconds. Deriving it makes it
    // phase-locked by construction, exactly like the SH-101's flip-flop
    // divider hanging off the VCO.
    const auto subPhase = 0.5f * t + (subHigh ? 0.5f : 0.0f);
    const auto subDt = 0.5f * dt;

    auto subFallingPhase = subPhase + 0.5f;
    if (subFallingPhase >= 1.0f)
        subFallingPhase -= 1.0f;

    // Always 50% duty, so unlike the pulse it needs no width clamp and
    // carries no DC.
    frame.sub = (subPhase < 0.5f ? 1.0f : -1.0f)
              + polyBlep (subPhase,        subDt)
              - polyBlep (subFallingPhase, subDt);

    // Phase advances once, after every tap has read it.
    phase += phaseIncrement;
    if (phase >= 1.0)
    {
        phase -= 1.0;
        subHigh = ! subHigh; // divide by two
    }

    return frame;
}
