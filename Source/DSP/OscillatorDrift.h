#pragma once

// So JUCE_DEBUG is defined before the #if JUCE_DEBUG block at the bottom of
// this header regardless of what a given translation unit has included
// before this header - same self-contained pattern StepClock.h uses.
#include <juce_core/juce_core.h>

#include <cmath>

#include "NoiseGenerator.h"

//==============================================================================
/*
    character-and-vim.md A3 (item 10) - "real VCOs are never perfectly
    stable". A bounded, slowly-wandering random walk: a one-pole leaky
    integrator fed by white noise, NOT a free-running accumulator - the
    leak (decayCoeff < 1) is what keeps it bounded instead of drifting off
    forever, which is exactly what makes it safe to add into a phase or a
    pitch without any hard clamp ever needing to engage in practice (one is
    still applied, defensively).

    Hand-rolled, like every other DSP block in this codebase - full control,
    no need to verify a library implementation's exact behaviour by reading
    its sources.

    Produces a value nominally in roughly [-1, 1] (occasionally a little
    outside, statistically rare, hard-clamped regardless) - UNSCALED. Each
    caller applies its own magnitude: SynthVoice scales this into a small
    number of OCTAVES for the main oscillator's pitch; PolyBlepOscillator
    scales a SECOND, independently-seeded instance into a small PHASE
    fraction added directly to the sub-oscillator's own derived phase (see
    that class's own comment for why sub needs a phase offset rather than a
    frequency offset - it has no independently settable frequency).
*/
class OscillatorDrift
{
public:
    // Seed is settable, same reasoning as every other seeded generator in
    // this codebase (NoiseGenerator's own header comment) - two instances in
    // the same signal path (main and sub) must not wander in lockstep.
    explicit OscillatorDrift (std::uint32_t seed) noexcept : noise (seed) {}

    void prepare (double newSampleRate) noexcept
    {
        // BY EAR, not derived - same posture as every other shaping constant
        // in this codebase (Vcf's maxFeedback, Adsr's exponentialTauFraction,
        // Chorus's lfoRateHz, ...). A few seconds is "slow wander", not a
        // vibrato rate - the whole point is that it reads as instability, not
        // a deliberate modulation.
        constexpr float wanderSeconds = 4.0f;

        // AR(1) leaky-integrator coefficients (documents/character-and-vim.md
        // A3's own build note has the derivation): `decayCoeff` sets how
        // "sticky" the walk is - close to 1 means slow, smooth wander rather
        // than sample-to-sample jitter. `stepGain` is sized so the walk's
        // STEADY-STATE standard deviation comes out to ~1.0 (unscaled,
        // Var(white noise in [-1,1)) = 1/3, so stepGain =
        // sqrt(3 * (1 - decayCoeff^2)) makes Var(state) -> 1 at steady
        // state), which is what makes "nominally in roughly [-1, 1]" true
        // regardless of sample rate or wanderSeconds.
        decayCoeff = std::exp (-1.0f / (wanderSeconds * (float) newSampleRate));
        stepGain = std::sqrt (juce::jmax (0.0f, 3.0f * (1.0f - decayCoeff * decayCoeff)));

        reset();
    }

    void reset() noexcept { state = 0.0f; }

    float processSample() noexcept
    {
        state = state * decayCoeff + noise.processSample() * stepGain;

        // Defensive hard bound - the leaky integrator's steady state is
        // close to [-1, 1] but a random walk can statistically wander
        // further on rare, long tails. Clamped rather than trusted, the same
        // "bounded by construction, not by luck" posture Vcf's own feedback
        // solve takes.
        return juce::jlimit (-2.0f, 2.0f, state);
    }

private:
    float decayCoeff = 0.0f;
    float stepGain = 0.0f;
    float state = 0.0f;
    NoiseGenerator noise;
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    Covers the generator in isolation: stays finite and within the
    documented bound over a long render, and two independently-seeded
    instances actually diverge (the whole point of giving main/sub distinct
    seeds) rather than producing the same sequence.
*/
void runOscillatorDriftSelfTest();

#endif
