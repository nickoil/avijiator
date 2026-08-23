#pragma once

#include "TptSvfStage.h"

//==============================================================================
/*
    24dB resonant lowpass: two 12dB TPT SVF stages in cascade.

    Each stage runs at FIXED critical damping (k = 2), which makes one stage
    1/(s+1)^2 and the cascade 1/(s+1)^4 - four coincident real poles, the same
    pole placement a four-pole ladder has, built from stages we own.

    Resonance deliberately does NOT live in the per-stage damping. Step 6 adds
    it as a single global feedback path around the whole cascade, which is
    what gives an exactly derivable self-oscillation threshold and one obvious
    place to add saturation later.

    Step 5 (Filter): plain cascade, no feedback path yet.
    See documents/dsp-voice-design.md section 3.
*/
class Vcf
{
public:
    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    // Takes cutoff in log2(Hz) - the caller sums its modulation in the octave
    // domain and exp2 happens in here, once, after that sum.
    float processSample (float input, float cutoffLog2Hz) noexcept;

private:
    TptSvfCoefficients makeCoefficients (float cutoffHz) const noexcept;

    static constexpr float stageDamping = 2.0f;

    static constexpr float minCutoffHz = 20.0f;
    static constexpr float maxCutoffHz = 18000.0f;

    double sampleRate = 0.0;

    // tan() blows up approaching Nyquist, so the upper cutoff limit is also
    // capped at a fraction of the sample rate - see prepare().
    float upperCutoffHz = maxCutoffHz;

    TptSvfStage stage1;
    TptSvfStage stage2;
};
