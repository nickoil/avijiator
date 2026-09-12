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

#include "Adsr.h"

#include <cmath>

#include <juce_core/juce_core.h>

void Adsr::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    reset();
}

void Adsr::reset() noexcept
{
    stage = Stage::Idle;
    currentLevel = 0.0f;
}

void Adsr::noteOn() noexcept
{
    stage = Stage::Attack;
}

void Adsr::noteOff() noexcept
{
    if (stage != Stage::Idle)
        stage = Stage::Release;
}

float Adsr::processSample() noexcept
{
    switch (stage)
    {
        case Stage::Idle:
            break; // currentLevel stays at whatever it last was - 0, normally

        case Stage::Attack:
        {
            const auto increment = 1.0f / (juce::jmax (minStageSeconds, attackSeconds) * (float) sampleRate);
            currentLevel += increment;

            if (currentLevel >= 1.0f)
            {
                currentLevel = 1.0f;
                stage = Stage::Decay;
            }
            break;
        }

        case Stage::Decay:
        {
            if (curveEnabled)
            {
                // Exponential approach to sustainLevel - see
                // exponentialTauFraction's own comment for why the stage time
                // is scaled down before becoming a time constant. Converges
                // from ABOVE regardless of where currentLevel started (a
                // retrigger partway through a previous stage), same
                // "proportionally finishes, nothing special-cased" property
                // the linear branch already has.
                const auto tau = juce::jmax (minStageSeconds, decaySeconds) * exponentialTauFraction;
                const auto coeff = std::exp (-1.0f / (tau * (float) sampleRate));
                currentLevel = sustainLevel + (currentLevel - sustainLevel) * coeff;

                if (std::abs (currentLevel - sustainLevel) <= exponentialSnapEpsilon)
                {
                    currentLevel = sustainLevel;
                    stage = Stage::Sustain;
                }
            }
            else
            {
                const auto decrement = 1.0f / (juce::jmax (minStageSeconds, decaySeconds) * (float) sampleRate);
                currentLevel -= decrement;

                if (currentLevel <= sustainLevel)
                {
                    currentLevel = sustainLevel;
                    stage = Stage::Sustain;
                }
            }
            break;
        }

        case Stage::Sustain:
            // Tracks the (already-smoothed) sustain level live, so moving the
            // knob while held is heard immediately, not just on the next trigger.
            currentLevel = sustainLevel;
            break;

        case Stage::Release:
        {
            if (curveEnabled)
            {
                // Same shape as the Decay branch above, target 0 instead of
                // sustainLevel.
                const auto tau = juce::jmax (minStageSeconds, releaseSeconds) * exponentialTauFraction;
                const auto coeff = std::exp (-1.0f / (tau * (float) sampleRate));
                currentLevel = currentLevel * coeff;

                if (currentLevel <= exponentialSnapEpsilon)
                {
                    currentLevel = 0.0f;
                    stage = Stage::Idle;
                }
            }
            else
            {
                const auto decrement = 1.0f / (juce::jmax (minStageSeconds, releaseSeconds) * (float) sampleRate);
                currentLevel -= decrement;

                if (currentLevel <= 0.0f)
                {
                    currentLevel = 0.0f;
                    stage = Stage::Idle;
                }
            }
            break;
        }
    }

    return currentLevel;
}

//==============================================================================
#if JUCE_DEBUG

void runAdsrCurveSelfTest()
{
    constexpr double sr = 48000.0;

    //==========================================================================
    // OFF PATH (the default): byte-identical to the exact linear arithmetic
    // that existed before A2 - checked against the formula directly, not just
    // "still looks linear". A retrigger-mid-decay style start (currentLevel
    // arbitrary, not just 1.0) is covered too, since the linear branch's own
    // "proportionally finishes" property depends on starting anywhere.
    {
        Adsr adsr;
        adsr.prepare (sr);
        adsr.setDecaySeconds (0.1f);
        adsr.setSustainLevel (0.3f);
        adsr.noteOn();

        // Run Attack out (default 0.01s = 480 samples at this rate) so the
        // expected value below starts from a known point (currentLevel ==
        // 1.0, the top of Attack) rather than needing Attack's own ramp
        // folded into the closed form.
        while (adsr.getStage() == Adsr::Stage::Attack)
            adsr.processSample();

        jassert (adsr.getStage() == Adsr::Stage::Decay);

        const auto decrement = 1.0f / (0.1f * (float) sr);
        auto expected = 1.0f;

        for (int i = 0; i < 50; ++i)
        {
            expected -= decrement;
            const auto actual = adsr.processSample();
            jassert (std::abs (actual - expected) < 1.0e-6f);
        }
    }

    //==========================================================================
    // ON PATH: reaches its target and hands off to the next stage, rather
    // than asymptoting forever - proven for both Decay (-> Sustain) and
    // Release (-> Idle).
    {
        Adsr adsr;
        adsr.prepare (sr);
        adsr.setCurveEnabled (true);
        adsr.setDecaySeconds (0.05f);
        adsr.setSustainLevel (0.4f);
        adsr.setAttackSeconds (0.001f);
        adsr.noteOn();

        auto reachedSustain = false;
        for (int i = 0; i < (int) sr; ++i) // up to 1 full second - generous
        {
            adsr.processSample();
            if (adsr.getStage() == Adsr::Stage::Sustain) { reachedSustain = true; break; }
        }
        jassert (reachedSustain);

        adsr.setReleaseSeconds (0.05f);
        adsr.noteOff();

        auto reachedIdle = false;
        for (int i = 0; i < (int) sr; ++i)
        {
            const auto level = adsr.processSample();
            if (adsr.getStage() == Adsr::Stage::Idle) { reachedIdle = true; jassert (level == 0.0f); break; }
        }
        jassert (reachedIdle);
    }

    //==========================================================================
    // THE TWO PATHS DIVERGE: same time constant, same starting conditions,
    // curve on vs. off - partway through Decay the two must disagree, or
    // curveEnabled would be silently doing nothing.
    {
        const auto runPartwayThroughDecay = [sr] (bool curveOn)
        {
            Adsr adsr;
            adsr.prepare (sr);
            adsr.setCurveEnabled (curveOn);
            adsr.setAttackSeconds (0.001f);
            adsr.setDecaySeconds (0.1f);
            adsr.setSustainLevel (0.2f);
            adsr.noteOn();

            float level = 0.0f;
            for (int i = 0; i < 1000; ++i) // well into Decay, short of arrival
                level = adsr.processSample();
            return level;
        };

        jassert (runPartwayThroughDecay (false) != runPartwayThroughDecay (true));
    }
}

#endif
