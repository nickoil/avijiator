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

//==============================================================================
/*
    Triangle / square / sample-and-hold / ramp / sine LFO. One phase
    accumulator, same style as the audio oscillator, but no PolyBLEP - LFO
    rates are sub-audio, so aliasing doesn't apply at the ranges this covers.
    That reasoning covers Ramp's hard discontinuity the same way it already
    covered Square's.

    Sample-and-hold: draws a new random value from its own seeded
    NoiseGenerator exactly at each phase wrap, holds it constant until the
    next one - classic stepped/glitchy character, not a smoothed random walk.

    Ramp added at the user's request: rises linearly -1 -> +1 across the
    cycle, then resets - the "ramp up" convention, not "ramp down"/reverse
    saw. Easy to flip (negate `value`, or reverse the -1..+1 traversal) if
    the opposite direction turns out to be what was actually wanted - flagged
    here rather than silently assumed, since either reading of "ramp" is
    common LFO terminology.

    Full derivation in documents/envelope-lfo-design.md section 3 (original
    three waveforms only - Ramp/Sine are a later, undocumented-in-that-doc
    addition).
*/
class Lfo
{
public:
    // Order matches the UI combo box exactly (SynthPanel.cpp's
    // lfoWaveformChoices), the user's own choice of display order - every
    // reference to a specific waveform elsewhere in the codebase (this
    // file's own switch, VoiceParameters.h's default, the tempo-sync
    // self-test) is by name, not by underlying int, so this order is free to
    // change without touching any of them.
    enum class Waveform { Ramp, Triangle, Sine, Square, SampleAndHold };

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    void setRate (float rateHz) noexcept;
    void setWaveform (Waveform newWaveform) noexcept { waveform = newWaveform; }

    // Bipolar, -1..1.
    float processSample() noexcept;

private:
    double inverseSampleRate = 0.0;
    double phase = 0.0;
    double phaseIncrement = 0.0;

    Waveform waveform = Waveform::Triangle;

    // Distinct seed from the voice's audible noise source and the filter's
    // floor noise, so none of the three noise streams are correlated.
    NoiseGenerator sampleAndHoldNoise { 0x27d4eb2fu };
    float heldRandomValue = 0.0f;
};
