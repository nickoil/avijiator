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

#include "NoiseGenerator.h"
#include "TptSvfStage.h"

//==============================================================================
/*
    24dB resonant lowpass: two 12dB TPT SVF stages in cascade.

    Each stage runs at FIXED critical damping (k = 2), which makes one stage
    1/(s+1)^2 and the cascade 1/(s+1)^4 - four coincident real poles, the same
    pole placement a four-pole ladder has, built from stages we own.

    Resonance deliberately does NOT live in the per-stage damping. It is a
    single global feedback path around the whole cascade, solved zero-delay -
    which gives an exactly derivable self-oscillation threshold.

    Evaluating the cascade at the cutoff (s = j) gives (j+1)^4 = -4, i.e.
    magnitude 1/4 at exactly 180 degrees - so negative feedback becomes
    POSITIVE at the cutoff, and unity loop gain (self-oscillation) lands at
    k = 4. That is the same reason a Moog ladder's resonance runs 0..4.

    A linear filter does not self-oscillate at a steady amplitude above that
    threshold - it diverges to inf/NaN, since nothing bounds the loop. softClip
    on the feedback path (below) is what makes k > 4 usable at all: it is a
    STABILITY requirement, found by ear when full resonance killed the voice
    with no recovery, not the flavour a "drive stage" TODO once described.
    That TODO was tried here for character-and-vim.md A1 (a drive stage
    inside this feedback loop, scaled by a caller-supplied amount) and
    REMOVED again - the user found it pointless, it only ever coloured
    cutoff/resonance interaction and never read as real distortion. A1 is now
    a plain pre-filter drive/distortion stage instead (Source/DSP/Drive.h,
    applied to the oscillator mix in SynthVoice before it ever reaches here)
    - this class is back to exactly what it was before that attempt.

    See documents/dsp-voice-design.md section 3.
*/
class Vcf
{
public:
    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    // Takes cutoff in log2(Hz) - the caller sums its modulation in the octave
    // domain and exp2 happens in here, once, after that sum. Resonance is
    // 0..1, mapped onto the feedback gain internally.
    float processSample (float input, float cutoffLog2Hz, float resonance01) noexcept;

private:
    TptSvfCoefficients makeCoefficients (float cutoffHz) const noexcept;

    //==============================================================================
    // Bounds the feedback loop. NOT flavour - this is a stability requirement.
    // Above k = 4 the linear loop gain exceeds unity, so a purely linear
    // filter does not self-oscillate at a steady amplitude, it grows without
    // bound until it reaches inf and then NaN, and NaN states latch forever.
    // Real analogue is bounded by transistor saturation; this is the
    // equivalent.
    //
    // Deliberately EXACTLY linear below the threshold so the signal stays
    // vanilla at normal levels - it only engages when the loop is running
    // away. A1's drive stage (see the class comment) has already been tried
    // and removed from here - this is stability-only again.
    static float softClip (float x) noexcept;

    static constexpr float softClipThreshold = 1.0f;

    static constexpr float stageDamping = 2.0f;

    static constexpr float minCutoffHz = 20.0f;
    static constexpr float maxCutoffHz = 18000.0f;

    //==============================================================================
    // BY EAR, not derived - see documents/dsp-voice-design.md section 8.

    // Unity loop gain is exactly 4.0; the overshoot puts the top of the knob
    // firmly on the oscillating side rather than sitting on the threshold.
    static constexpr float maxFeedback = 4.5f;

    // Feedback drops DC gain to 1/(1+k). Analogue ladders lose bass the same
    // way, so only part of it is given back: 0 = full analogue bass loss,
    // 1 = level held flat, which sounds thin and clinical.
    static constexpr float resonanceCompensation = 0.5f;

    // -120 dBFS. A perfectly zero input into a perfectly zero state stays
    // zero forever, so self-oscillation would never start. Real analogue
    // starts from thermal noise; this is the equivalent. It also keeps the
    // integrator states above denormal range.
    static constexpr float noiseFloorAmplitude = 1.0e-6f;

    double sampleRate = 0.0;

    // tan() blows up approaching Nyquist, so the upper cutoff limit is also
    // capped at a fraction of the sample rate - see prepare().
    float upperCutoffHz = maxCutoffHz;

    TptSvfStage stage1;
    TptSvfStage stage2;

    // Distinct seed so this does not emit the same sequence as the voice's
    // audible noise source.
    NoiseGenerator floorNoise { 0x5bf03635u };
};
