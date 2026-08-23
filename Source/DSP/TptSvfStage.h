#pragma once

//==============================================================================
/*
    Coefficients for one TPT SVF stage. Computed once per sample by Vcf and
    shared across both stages of the cascade, so there is one tan() per sample
    rather than two.
*/
struct TptSvfCoefficients
{
    float a1 = 0.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
};

//==============================================================================
/*
    One 12dB topology-preserving-transform state variable filter stage, in
    Simper's form. Owns nothing but its two trapezoidal integrator states -
    everything frequency-dependent lives in the coefficients passed in.

    Derivation in documents/dsp-voice-design.md section 3.
*/
class TptSvfStage
{
public:
    void reset() noexcept
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    //==============================================================================
    // The lowpass output is AFFINE in its input:
    //
    //     v2 = ic2eq + a2*ic1eq + a3*(in - ic2eq)
    //        = a3*in + [(1 - a3)*ic2eq + a2*ic1eq]
    //        = G*in  + S
    //
    // Vcf uses these two halves to solve its resonance feedback loop
    // algebraically, with no unit delay in the loop.
    static float getInstantaneousGain (const TptSvfCoefficients& c) noexcept
    {
        return c.a3;
    }

    float getStateContribution (const TptSvfCoefficients& c) const noexcept
    {
        return (1.0f - c.a3) * ic2eq + c.a2 * ic1eq;
    }

    float processLowpass (float input, const TptSvfCoefficients& c) noexcept
    {
        const auto v3 = input - ic2eq;
        const auto v1 = c.a1 * ic1eq + c.a2 * v3;
        const auto v2 = ic2eq + c.a2 * ic1eq + c.a3 * v3;

        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        // Lowpass tap. Bandpass would be v1 and highpass input - k*v1 - v2,
        // one line each if they are ever wanted.
        return v2;
    }

private:
    // These decaying toward zero after the input stops is the classic
    // denormal trap - juce::ScopedNoDenormals in the audio callback is what
    // keeps that from costing hundreds of cycles per sample.
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
};
