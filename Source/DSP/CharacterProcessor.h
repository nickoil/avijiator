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

#include <cmath>

#include "NoiseGenerator.h"

//==============================================================================
/*
    character-and-vim.md's Tier 1 "Vim" switch - "a single vimEnabled gating
    a CharacterProcessor block" is that document's own name for exactly this:
    the always-tasteful, no-sub-parameters colouration bundled behind one
    checkbox. A2 (Adsr::setCurveEnabled) is the first Tier 1 mechanism and
    lives in Adsr itself, since it IS the envelope; everything else that
    doesn't already have a natural home lives here.

    Two independent pieces, both gated the SAME way (a bool the caller sets
    once per block from VoiceParameters::vimEnabled, off by default, exactly
    a pass-through/no-op when off):

    - curvedVelocity(): a pure function, B5/A6's velocity response curve -
      used by SynthVoice, once per block (currentVelocity is constant across
      a block - see that class's own comment).
    - CharacterProcessor: a small stateful output-stage block - B4's noise
      floor and output saturation/asymmetric clipping - owned by
      MainComponent and applied to the mono mix once per sample, BEFORE
      Chorus (B1) - real analogue signal order is saturation/glue first,
      stereo widening after.
*/

// A1's drive stage lives in Source/DSP/Drive.h now, as a pre-filter stage -
// see that file's own comment for its revision history. This is a DIFFERENT
// curve - velocity's own response shape, character-and-vim.md A6: "small
// deltas at low values are less audible than the same delta at
// high values" is a convex curve; x^2 is the simplest one that satisfies it
// while leaving both endpoints (0 -> 0, 1 -> 1) exactly where they already
// were, so a full-velocity note is unaffected either way. BY EAR, not
// derived - same posture as every other shaping choice in this codebase.
inline float curvedVelocity (float velocity01) noexcept
{
    return velocity01 * velocity01;
}

class CharacterProcessor
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset() noexcept { /* stateless besides the RNG, which needs no reset between blocks */ }

    // Discrete switch, called once per block from MainComponent - same
    // treatment as every other vimEnabled-gated switch in this codebase
    // (Adsr::setCurveEnabled). False (the default) makes processSample an
    // exact pass-through.
    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }

    float processSample (float input) noexcept
    {
        if (! enabled)
            return input;

        // Noise floor: -70dBFS broadband hiss - counterintuitive, but a
        // perfectly silent digital noise floor reads as synthetic (the
        // doc's own framing). 10^(-70/20).
        constexpr float noiseFloorAmplitude = 3.162e-4f;
        const auto withFloor = input + noise.processSample() * noiseFloorAmplitude;

        return asymmetricSoftClip (withFloor);
    }

private:
    // Real transistor/diode circuits clip positive and negative swings
    // differently, producing even-harmonic content that reads as "warm"
    // rather than "distorted" (B4's own framing) - a symmetric clipper (like
    // Vcf's own softClip, or Chorus's none) only ever produces odd
    // harmonics. Both branches are `tanh(x*drive)/drive`, which has
    // small-signal gain EXACTLY 1 regardless of drive (the derivative of
    // tanh(dx)/d at x=0 is d*sech^2(0)/d = 1) - so the two branches meet
    // continuously and with matching slope at x == 0, and normal-level
    // signal is untouched; only the PEAKS saturate, and asymmetrically. BY
    // EAR, not derived - same posture as every other shaping constant here.
    static float asymmetricSoftClip (float x) noexcept
    {
        constexpr float positiveDrive = 1.15f;
        constexpr float negativeDrive = 0.90f;

        return x >= 0.0f ? std::tanh (x * positiveDrive) / positiveDrive
                          : std::tanh (x * negativeDrive) / negativeDrive;
    }

    double sampleRate = 44100.0;
    bool enabled = false;

    // Distinct seed from every other seeded generator in this codebase -
    // must not emit the same sequence as the filter's floor noise, the
    // voice's own audible noise source, the arp/seq's humanise generators,
    // or the oscillator drift generators.
    NoiseGenerator noise { 0x4a7e21c8u };
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    Covers both pieces: curvedVelocity's endpoints and monotonicity, and
    CharacterProcessor's own behaviour - an exact pass-through when disabled
    (the default), and, when enabled, a real, bounded, finite, non-silent
    change to the signal (the noise floor and saturation are actually
    reaching the output).
*/
void runCharacterProcessorSelfTest();

#endif
