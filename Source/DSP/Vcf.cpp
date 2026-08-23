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

float Vcf::processSample (float input, float cutoffLog2Hz) noexcept
{
    const auto cutoffHz = juce::jlimit (minCutoffHz, upperCutoffHz, std::exp2 (cutoffLog2Hz));
    const auto c = makeCoefficients (cutoffHz);

    // Plain series cascade - no resonance yet. Step 6 replaces this line with
    // the zero-delay solve of a global feedback loop around both stages.
    return stage2.processLowpass (stage1.processLowpass (input, c), c);
}
