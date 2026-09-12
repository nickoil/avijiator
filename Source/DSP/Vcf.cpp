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

#include "Vcf.h"

#include <cmath>

#include <juce_core/juce_core.h>

void Vcf::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;

    // 0.45*fs keeps the prewarped integrator gain bounded: at that point
    // g = tan(81 degrees) ~ 6.3, which is comfortable. Any closer to Nyquist
    // and tan() runs away.
    upperCutoffHz = juce::jmin (maxCutoffHz, (float) (0.45 * sampleRate));

    reset();
}

void Vcf::reset() noexcept
{
    stage1.reset();
    stage2.reset();
}

float Vcf::softClip (float x) noexcept
{
    // Identity inside the threshold, tanh-shaped outside it, and continuous
    // at the join (tanh(0) = 0). Output is bounded to +/-(threshold + 1).
    if (x > softClipThreshold)
        return softClipThreshold + std::tanh (x - softClipThreshold);

    if (x < -softClipThreshold)
        return -softClipThreshold + std::tanh (x + softClipThreshold);

    return x;
}

TptSvfCoefficients Vcf::makeCoefficients (float cutoffHz) const noexcept
{
    // Prewarped integrator gain - maps the analogue cutoff onto the bilinear
    // frequency axis so the digital cutoff lands where it is asked to.
    const auto g = std::tan (juce::MathConstants<float>::pi * cutoffHz / (float) sampleRate);

    TptSvfCoefficients c;
    c.a1 = 1.0f / (1.0f + g * (g + stageDamping));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;

    return c;
}

float Vcf::processSample (float input, float cutoffLog2Hz, float resonance01) noexcept
{
    const auto cutoffHz = juce::jlimit (minCutoffHz, upperCutoffHz, std::exp2 (cutoffLog2Hz));
    const auto c = makeCoefficients (cutoffHz);

    const auto k = resonance01 * maxFeedback;

    // Partial makeup for the passband level the feedback costs, plus the
    // noise floor self-oscillation needs to start from.
    const auto in = input * (1.0f + resonanceCompensation * k)
                  + floorNoise.processSample() * noiseFloorAmplitude;

    // Both stages share the same coefficients, so their instantaneous gain is
    // identical and the cascade gain is simply that squared.
    const auto stageGain = TptSvfStage::getInstantaneousGain (c);
    const auto cascadeGain = stageGain * stageGain;
    const auto cascadeState = stageGain * stage1.getStateContribution (c)
                            + stage2.getStateContribution (c);

    // Closing the loop u = x - k*y on an affine cascade y = Gt*u + St gives
    //     y = (Gt*x + St) / (1 + k*Gt)
    // solved algebraically rather than with a unit delay. No delay in the
    // loop means no cutoff-dependent resonance detuning and no delay-induced
    // blow-up. The denominator is >= 1 for k >= 0, so this never divides by
    // zero - the filter can still genuinely self-oscillate, which is the
    // intent, but it cannot blow up numerically.
    const auto solved = (cascadeGain * in + cascadeState) / (1.0f + k * cascadeGain);

    // Run the stages forward with the solved loop input, so the integrator
    // states advance consistently with the value just computed. The soft clip
    // is what stops the loop running away once k is past the self-oscillation
    // threshold: it caps the energy entering the stages, so the oscillation
    // settles into a bounded limit cycle instead of diverging to NaN.
    //
    // Below the clip threshold this is exactly `in - k * solved` and the
    // forward pass reproduces `solved` to within rounding. Above it the two
    // legitimately differ - which is the point - so there is no equality
    // assert here.
    const auto stageInput = softClip (in - k * solved);
    const auto result = stage2.processLowpass (stage1.processLowpass (stageInput, c), c);

    // Finiteness is the invariant that actually matters: a single non-finite
    // sample latches the integrator states permanently, and the synth goes
    // silent with no way back short of a restart.
    jassert (std::isfinite (result));

    return result;
}
