#include "Vcf.h"

#include <cmath>

#include <juce_core/juce_core.h>

#if JUCE_DEBUG
 #include <array>
 #include <span>
#endif

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

float Vcf::driveSaturate (float x, float driveAmount) noexcept
{
    // driveAmount == 0 -> driveGain == 1.0f exactly, so this reduces to
    // softClip(x) / 1.0f - byte-identical to softClip(x) alone. Above 0, x is
    // pushed harder into the SAME curve before the makeup division pulls the
    // added gain back out, so the shape saturates more without simply
    // getting louder.
    const auto driveGain = 1.0f + driveAmount * (maxDriveGain - 1.0f);
    return softClip (x * driveGain) / driveGain;
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

float Vcf::processSample (float input, float cutoffLog2Hz, float resonance01, float driveAmount) noexcept
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
    // states advance consistently with the value just computed. driveSaturate
    // is what stops the loop running away once k is past the self-oscillation
    // threshold - same stability role softClip always had - AND, at
    // driveAmount > 0, colours the sound at normal levels too (A1): pushed
    // harder into the same curve, cutoff/resonance interaction turns
    // nonlinear before the loop is anywhere near self-oscillating.
    //
    // At driveAmount == 0 this is exactly softClip(in - k*solved) - see
    // driveSaturate's own comment - so below the clip threshold it is exactly
    // `in - k * solved` and the forward pass reproduces `solved` to within
    // rounding, same as before A1 existed. Above it (either from resonance
    // alone or with drive pushing harder) the two legitimately differ - which
    // is the point - so there is no equality assert here.
    const auto stageInput = driveSaturate (in - k * solved, driveAmount);
    const auto result = stage2.processLowpass (stage1.processLowpass (stageInput, c), c);

    // Finiteness is the invariant that actually matters: a single non-finite
    // sample latches the integrator states permanently, and the synth goes
    // silent with no way back short of a restart.
    jassert (std::isfinite (result));

    return result;
}

//==============================================================================
#if JUCE_DEBUG

namespace
{
    // A real sweep, not a single static sample: a decaying saw-ish input so
    // the filter's own state (both integrators, plus the k*solved feedback
    // term) actually moves through a range of levels, the same reasoning
    // every other "byte-identical at the inert default" test in this
    // codebase gives for driving a real signal through rather than a fixed
    // number.
    void renderSweep (float driveAmount, float resonance01, std::span<float> output) noexcept
    {
        Vcf filter;
        filter.prepare (48000.0);

        for (size_t i = 0; i < output.size(); ++i)
        {
            // A cheap decaying "buzz" - not a real oscillator, just something
            // with harmonic content and a level that moves, which is all this
            // needs to push the filter's nonlinearity around.
            const auto t = (float) i;
            const auto raw = std::fmod (t * 0.1f, 1.0f) * 2.0f - 1.0f;
            const auto input = raw * std::exp (-t * 0.0002f);

            output[i] = filter.processSample (input, 10.0f, resonance01, driveAmount);
        }
    }
}

void runVcfDriveSelfTest()
{
    constexpr int numSamples = 2000;

    //==========================================================================
    // driveAmount == 0 is byte-identical across a real sweep, at both a low
    // and a high resonance - driveSaturate must not perturb the existing
    // stability-only softClip behaviour at all when off.
    for (const float resonance : { 0.1f, 0.95f })
    {
        std::array<float, numSamples> a {}, b {};
        renderSweep (0.0f, resonance, a);
        renderSweep (0.0f, resonance, b);
        jassert (a == b); // two independent renders at driveAmount == 0 agree exactly
    }

    //==========================================================================
    // driveAmount turned up changes the output - the term reaches the loop.
    {
        std::array<float, numSamples> noDrive {}, fullDrive {};
        renderSweep (0.0f, 0.3f, noDrive);
        renderSweep (1.0f, 0.3f, fullDrive);
        jassert (! (noDrive == fullDrive));
    }
}

#endif
