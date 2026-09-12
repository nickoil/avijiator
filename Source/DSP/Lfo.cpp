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

#include "Lfo.h"

#include <cmath>

#include <juce_core/juce_core.h>

void Lfo::prepare (double newSampleRate) noexcept
{
    inverseSampleRate = 1.0 / newSampleRate;
    reset();
}

void Lfo::reset() noexcept
{
    phase = 0.0;

    // Reset the generator too (not just re-draw), so a fresh reset gives a
    // reproducible sequence - the same reason NoiseGenerator's seed exists.
    sampleAndHoldNoise.reset();
    heldRandomValue = sampleAndHoldNoise.processSample();
}

void Lfo::setRate (float rateHz) noexcept
{
    phaseIncrement = (double) rateHz * inverseSampleRate;
}

float Lfo::processSample() noexcept
{
    float value = 0.0f;

    switch (waveform)
    {
        case Waveform::Triangle:
        {
            // Symmetric ramp: -1 at phase 0, +1 at phase 0.5, back to -1 at
            // the wrap. Both halves agree at phase 0.5, so there's no
            // discontinuity at the peak.
            const auto t = (float) phase;
            value = t < 0.5f ? (-1.0f + 4.0f * t) : (3.0f - 4.0f * t);
            break;
        }

        case Waveform::Square:
            value = phase < 0.5 ? 1.0f : -1.0f;
            break;

        case Waveform::SampleAndHold:
            value = heldRandomValue;
            break;

        case Waveform::Ramp:
            // Linear -1 at phase 0 to (just short of) +1 at the wrap - "ramp
            // up", see the class comment. Hard reset back to -1 at the wrap,
            // same un-anti-aliased treatment as Square above.
            value = -1.0f + 2.0f * (float) phase;
            break;

        case Waveform::Sine:
            value = std::sin (juce::MathConstants<float>::twoPi * (float) phase);
            break;
    }

    phase += phaseIncrement;
    if (phase >= 1.0)
    {
        phase -= 1.0;

        // New value takes effect immediately at the first sample of the new
        // cycle - drawn here (after this sample's value was already read
        // above), held until the next wrap.
        if (waveform == Waveform::SampleAndHold)
            heldRandomValue = sampleAndHoldNoise.processSample();
    }

    return value;
}
