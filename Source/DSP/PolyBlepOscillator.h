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

#include "OscillatorDrift.h"

//==============================================================================
/*
    One phase accumulator, several taps — mirrors a real SH-101 VCO, where saw,
    pulse and the sub-divider all hang off a single oscillator. Phase advances
    exactly once per sample and every tap reads that same phase, so the taps
    cannot drift apart.

    Full PolyBLEP derivation in documents/dsp-voice-design.md section 2.

    All three taps present: saw, pulse/PWM, and the sub-oscillator one octave
    down. Noise is a separate generator (NoiseGenerator.h), not a tap off this
    phase.

    character-and-vim.md A3 (item 10) - the SUB-oscillator's own half of
    oscillator drift lives here, not in SynthVoice with the main oscillator's.
    Why: the sub has no independently settable frequency to add a pitch
    offset into - its phase is derived arithmetically from the main phase ON
    PURPOSE (see subHigh's own comment: a second free-running accumulator was
    explicitly rejected as a source of slow beating). Giving it independent
    drift therefore means a small, BOUNDED, slowly-wandering PHASE OFFSET
    added directly into the derived subPhase, rather than a frequency offset
    - see setDriftEnabled/subDrift below. This does not reintroduce the
    rejected second-accumulator problem: OscillatorDrift is a leaky
    integrator, not a free-running one, so it cannot slip phase against the
    main oscillator - it can only wobble within its own bounded range.
*/
class PolyBlepOscillator
{
public:
    struct Frame
    {
        float saw = 0.0f;
        float pulse = 0.0f;
        float sub = 0.0f;
    };

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    void setFrequency (float frequencyHz) noexcept;
    void setPulseWidth (float newPulseWidth) noexcept;

    // character-and-vim.md A3. Discrete switch, called once per block from
    // SynthVoice - same treatment as every other vimEnabled-gated switch in
    // this codebase (Adsr::setCurveEnabled). False (the default) makes the
    // sub-oscillator's phase offset always exactly 0.0f, byte-identical to
    // before A3.
    void setDriftEnabled (bool shouldBeEnabled) noexcept { subDriftEnabled = shouldBeEnabled; }

    Frame processSample() noexcept;

private:
    //==============================================================================
    // Band-limited step residual, scaled for a discontinuity of amplitude 2 —
    // which is exactly the saw's wrap (+1 -> -1) and also the pulse's edges,
    // so it is used unscaled for both.
    static float polyBlep (float t, float dt) noexcept;

    static constexpr float minFrequencyHz = 8.0f;

    // fs/4 rather than Nyquist: the sub-oscillator runs at half this
    // increment, and each BLEP correction window needs dt of room either side
    // of its edge. It also guarantees the duty clamp in processSample can
    // never invert - see the comment there.
    static constexpr double maxIncrement = 0.25;

    static constexpr float minPulseWidth = 0.02f;
    static constexpr float maxPulseWidth = 0.98f;

    double inverseSampleRate = 0.0;
    double phase = 0.0;
    double phaseIncrement = 0.0;

    float pulseWidth = 0.5f;

    // Divide-by-two flip-flop for the sub-oscillator, toggled on each main
    // phase wrap. The sub's phase is derived from this plus the main phase,
    // never accumulated separately - see processSample.
    bool subHigh = false;

    // BY EAR, not derived - same posture as every other shaping constant in
    // this codebase. A phase fraction, not a Hz/cents figure: 0.01 cycles is
    // a small, comfortably sub-audible-as-a-static-offset wobble whose SLOW
    // rate of change (OscillatorDrift's own multi-second time constant) is
    // what actually reads as pitch instability - see the class comment above
    // for why a constant offset alone would not.
    static constexpr float maxSubDriftPhaseFraction = 0.01f;

    bool subDriftEnabled = false;

    // Distinct seed from whatever main-oscillator drift generator the caller
    // (SynthVoice) owns separately, and from every other seeded generator in
    // this codebase - distinct seeds are what make main and sub "drift
    // against each other" rather than in lockstep.
    OscillatorDrift subDrift { 0x8e1c3f2au };
};
