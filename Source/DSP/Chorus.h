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

#include <array>
#include <cmath>

//==============================================================================
/*
    character-and-vim.md B1 - Juno-style ensemble/chorus. Turns SynthVoice's
    single mono output into a stereo pair via two modulated delay taps that
    share one LFO but read it at opposite phase, so left and right wander
    independently and the result is genuinely wide rather than a mono signal
    copied to both channels (which is what MainComponent's fan-out does when
    this is off).

    Hand-rolled, like every other DSP block in this codebase (Vcf, Adsr,
    PolyBlepOscillator, NoiseGenerator) - full control, no need to verify a
    library implementation's exact behaviour by reading its sources. Fixed
    delay-line size rather than one sized to the sample rate in prepare(): no
    allocation anywhere, matching CLAUDE.md's audio-thread constraints, and
    maxDelayMs is small enough that a generously-sized fixed buffer covers
    every realistic sample rate with headroom - see bufferSize's own comment.

    v1 SCOPE (documents/character-and-vim.md's settled scope): a plain on/off
    (VoiceParameters::chorusEnabled), fixed internal rate/depth constants
    below - no Rate/Depth knobs yet. MainComponent owns one instance and
    calls processSample once per output sample, only while chorusEnabled is
    on; off, the existing mono-copied-to-both-channels fan-out is untouched.
*/
class Chorus
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset() noexcept
    {
        buffer.fill (0.0f);
        writeIndex = 0;
        lfoPhase = 0.0f;
    }

    // Mono in, stereo out.
    void processSample (float input, float& outLeft, float& outRight) noexcept
    {
        buffer[(size_t) writeIndex] = input;

        // Hand-rolled sine LFO - no need to pull in the Lfo class's
        // triangle/square/S&H waveforms, which this has no use for. Left and
        // right read it pi apart, which is what gives them independently
        // wandering delay times rather than a mono modulated delay merely
        // duplicated to both channels.
        const auto lfoLeft  = std::sin (lfoPhase);
        const auto lfoRight = std::sin (lfoPhase + juce::MathConstants<float>::pi);

        const auto delaySamplesLeft  = (baseDelayMs + depthMs * lfoLeft)  * 0.001f * (float) sampleRate;
        const auto delaySamplesRight = (baseDelayMs + depthMs * lfoRight) * 0.001f * (float) sampleRate;

        const auto wetLeft  = readDelayed ((float) writeIndex - delaySamplesLeft);
        const auto wetRight = readDelayed ((float) writeIndex - delaySamplesRight);

        outLeft  = input * (1.0f - wetMix) + wetLeft  * wetMix;
        outRight = input * (1.0f - wetMix) + wetRight * wetMix;

        const auto lfoIncrement = (float) (juce::MathConstants<double>::twoPi * lfoRateHz / sampleRate);
        lfoPhase += lfoIncrement;
        if (lfoPhase >= juce::MathConstants<float>::twoPi)
            lfoPhase -= juce::MathConstants<float>::twoPi;

        writeIndex = (writeIndex + 1) % bufferSize;
    }

private:
    // Linearly-interpolated read at a fractional position, wrapped into the
    // ring buffer. readPositionSamples can be negative (a delay reaching back
    // past index 0) or larger than bufferSize (shouldn't happen given
    // maxDelayMs's margin, but wrapped defensively regardless).
    float readDelayed (float readPositionSamples) const noexcept
    {
        auto pos = std::fmod (readPositionSamples, (float) bufferSize);
        if (pos < 0.0f)
            pos += (float) bufferSize;

        // Defensive re-wrap: the += above adds a large constant (bufferSize)
        // to a much smaller value, and float32 rounding at that magnitude can
        // round a result just UNDER bufferSize UP to exactly bufferSize -
        // observed in practice as an out-of-bounds buffer[bufferSize] read
        // (MSVC's checked <array> caught it as "array subscript out of
        // range"). Not a rare corner case: this fires every time the read
        // position crosses zero, which happens on every LFO cycle.
        if (pos >= (float) bufferSize)
            pos -= (float) bufferSize;

        const auto index0 = (int) pos;
        const auto frac = pos - (float) index0;
        const auto index1 = (index0 + 1) % bufferSize;

        return buffer[(size_t) index0] * (1.0f - frac) + buffer[(size_t) index1] * frac;
    }

    //==============================================================================
    // BY EAR, not derived - same posture as every other shaping constant in
    // this codebase (Vcf's maxFeedback, Adsr's exponentialTauFraction, ...).
    // 0.5-3Hz is the classic ensemble sweep range; a 15ms base delay with 5ms
    // of sweep either side stays clear of both a flanger's much shorter
    // delays and any audible slap-back, and keeps the modulated tap
    // comfortably inside bufferSize with headroom.
    static constexpr float lfoRateHz = 0.6f;
    static constexpr float baseDelayMs = 15.0f;
    static constexpr float depthMs = 5.0f;
    static constexpr float wetMix = 0.5f;

    // Headroom over baseDelayMs + depthMs (20ms) so the modulated read
    // position is never within a few samples of wrapping into itself.
    static constexpr float maxDelayMs = baseDelayMs + depthMs + 5.0f;

    // Fixed at compile time rather than sized in prepare() - no allocation on
    // any thread. 8192 samples covers maxDelayMs (25ms) with margin at every
    // realistic audio sample rate up to ~300kHz; nothing in this project runs
    // anywhere near that.
    static constexpr int bufferSize = 8192;

    double sampleRate = 44100.0;
    std::array<float, (size_t) bufferSize> buffer {};
    int writeIndex = 0;
    float lfoPhase = 0.0f;
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    Covers character-and-vim.md B1's own instance: processSample actually
    produces a stereo signal (left and right diverge once the LFO has moved),
    and stays finite and bounded over a real render - the same "finiteness is
    the invariant that matters" posture Vcf::processSample's own jassert
    documents, checked here as an assertion rather than trusted by
    construction, since this is new state (a delay line and a running LFO
    phase) rather than a closed form.
*/
void runChorusSelfTest();

#endif
