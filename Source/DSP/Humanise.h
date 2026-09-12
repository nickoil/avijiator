/*
    This file is part of Avijiator.
    Copyright (C) 2026 Nick Casey

    Avijiator is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version.

    Avijiator is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with Avijiator. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

// So JUCE_DEBUG is defined before the #if JUCE_DEBUG block at the bottom of
// this header regardless of what a given translation unit has included
// before this header - same self-contained pattern StepClock.h uses.
#include <juce_core/juce_core.h>

//==============================================================================
/*
    character-and-vim.md B2 (item 10). Pure formulas shared by
    Arpeggiator::process and StepSequencer::process, each of which scales its
    own step timing/velocity by the SAME one VoiceParameters::humaniseAmount
    knob - see that atomic's own comment for why v1 is one knob rather than
    Swing/Timing Jitter/Velocity Jitter as three. Kept alongside
    StepClock.h's beatsPerStepForDivision as the shared-PURE-formula
    convention this codebase already uses (that table is reused by both
    consumers the same way) - the SCHEDULING LOOP each of Arpeggiator::process/
    StepSequencer::process wraps this arithmetic in stays independently
    duplicated, per StepClock.h's own comment on why: the two do different
    things at a step, only the numbers below are identical.

    Applied as a bounded ONSET DELAY after a step boundary is reached, using
    the same "independent countdown, take the min" shape each render loop
    already uses for its gate-off deadline - see each .cpp's own comment on
    the "third deadline". Never folded into the step grid itself (StepClock),
    which must stay exactly on time so swing/jitter can never accumulate step
    to step - see StepClock.h's own comment on why stepIndex (this is where
    swing's parity comes from) lives in the clock rather than in whichever
    class is sequencing.
*/
namespace Humanise
{
    // Odd steps only (swing) plus every step (timing jitter), each as a
    // fraction of ONE STEP's length. BY EAR, not derived - same posture as
    // every other shaping constant in this codebase (Vcf's maxFeedback,
    // Adsr's exponentialTauFraction, Chorus's lfoRateHz, ...). The two
    // fractions sum to well under 1.0 even at amount == 1, so a delayed
    // onset always resolves strictly before the NEXT step boundary arrives -
    // load-bearing for onsetDelaySamples' callers, which assume at most one
    // pending onset at a time.
    static constexpr float maxSwingFraction = 0.15f;
    static constexpr float maxJitterFraction = 0.03f;

    // Multiplicative, +/-, every step - applied directly to a note's velocity.
    static constexpr float maxVelocityJitter = 0.15f;

    // jitterRaw01 is one draw from a NoiseGenerator's processSample(), in
    // [-1, 1). Returns a sample count >= 0 - onset delay is deliberately
    // one-directional (a note can be pushed LATE, never played before its
    // own step boundary). At amount <= 0 this is EXACTLY 0 regardless of
    // jitterRaw01 - the short-circuit, not just an arithmetic identity - which
    // is what lets the caller's "fire immediately" branch stay byte-identical
    // to the code that existed before this feature.
    inline int onsetDelaySamples (double samplesPerStep, float amount, bool isOddStep, float jitterRaw01) noexcept
    {
        if (amount <= 0.0f)
            return 0;

        const auto swingFraction = isOddStep ? maxSwingFraction * amount : 0.0f;
        const auto jitterFraction = maxJitterFraction * amount * jitterRaw01;
        const auto totalFraction = juce::jmax (0.0f, swingFraction + jitterFraction);

        return (int) (totalFraction * (float) samplesPerStep + 0.5f);
    }

    // At amount == 0 this is EXACTLY 1.0f (1.0f + 0.0f * anything == 1.0f),
    // so a velocity multiplied by it is bit-for-bit unchanged.
    inline float velocityJitterFactor (float amount, float jitterRaw01) noexcept
    {
        return 1.0f + maxVelocityJitter * amount * jitterRaw01;
    }
}

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    Covers the pure arithmetic in isolation - Arpeggiator/StepSequencer's own
    self-tests cover it again at the integration level, through real rendered
    output, the same "belt and suspenders" split runFilterAutomationSelfTest
    and runStepSequencerRenderSelfTest already use for cutoff/resonance
    modulation.
*/
inline void runHumaniseFormulaSelfTest()
{
    //==========================================================================
    // amount == 0: EXACTLY inert, regardless of parity or jitter draw.
    jassert (Humanise::onsetDelaySamples (2000.0, 0.0f, true, 1.0f) == 0);
    jassert (Humanise::onsetDelaySamples (2000.0, 0.0f, false, -1.0f) == 0);
    jassert (Humanise::velocityJitterFactor (0.0f, 1.0f) == 1.0f);
    jassert (Humanise::velocityJitterFactor (0.0f, -1.0f) == 1.0f);

    //==========================================================================
    // Never negative, and strictly less than one full step even at the
    // maximum amount and the maximum jitter draw - the property
    // Arpeggiator/StepSequencer::process rely on to assume at most one
    // pending onset at a time.
    for (const bool oddStep : { false, true })
        for (const float jitterRaw : { -1.0f, 0.0f, 1.0f })
        {
            const auto delay = Humanise::onsetDelaySamples (2000.0, 1.0f, oddStep, jitterRaw);
            jassert (delay >= 0);
            jassert (delay < 2000);
        }

    //==========================================================================
    // Swing only applies on odd steps - at zero jitter, even steps get
    // exactly zero delay and odd steps get a positive one.
    jassert (Humanise::onsetDelaySamples (2000.0, 1.0f, false, 0.0f) == 0);
    jassert (Humanise::onsetDelaySamples (2000.0, 1.0f, true, 0.0f) > 0);

    //==========================================================================
    // Velocity jitter factor stays within the documented +/-15% band.
    jassert (Humanise::velocityJitterFactor (1.0f, 1.0f) <= 1.0f + Humanise::maxVelocityJitter + 1.0e-6f);
    jassert (Humanise::velocityJitterFactor (1.0f, -1.0f) >= 1.0f - Humanise::maxVelocityJitter - 1.0e-6f);
}

#endif
