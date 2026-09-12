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

//==============================================================================
/*
    Portamento: the pitch ramp between one note and the next.

    Hand-rolled rather than reusing juce::SmoothedValue, for a specific
    reason: SmoothedValue::reset(sampleRate, rampTime)'s snap-vs-not behaviour
    when called mid-ramp is not something we've verified, and CLAUDE.md
    forbids reading JUCE sources to check. Owning ~30 lines sidesteps the
    question entirely, and matches the precedent set by the oscillator,
    filter, ADSR and LFO.

    CONSTANT RATE, not constant time: glideTimeSeconds means "time to glide
    ONE OCTAVE", not "time to complete whichever interval is in flight". That
    is what makes this behave like Adsr's time constants - a knob move
    mid-glide changes only the rate of future samples, with zero
    special-casing for a ramp already in progress.

    Consequence, deliberate rather than incidental: a one-octave glide takes
    longer than a semitone glide. Real analogue portamento (a CV ramp at fixed
    rate) behaves the same way, so this is the authentic choice as well as the
    simple one.

    See documents/note-handling-design.md section 4.
*/
class Glide
{
public:
    void prepare (double newSampleRate) noexcept { sampleRate = newSampleRate; }

    void reset (float initialPitchLog2Hz) noexcept
    {
        currentPitchLog2Hz = initialPitchLog2Hz;
        targetPitchLog2Hz = initialPitchLog2Hz;
    }

    // Retargets in place, from wherever the ramp currently is - never snaps.
    // Mirrors Adsr::noteOn()'s "-> Attack, from wherever currentLevel is"
    // retrigger-safe philosophy. Use snapToTarget() for an instant jump.
    void setTarget (float newTargetPitchLog2Hz) noexcept
    {
        targetPitchLog2Hz = newTargetPitchLog2Hz;
    }

    // A note starting from silence should not slide in from whatever pitch
    // the ramp happened to be left at - possibly minutes ago. SynthVoice
    // calls this on a fresh trigger; see note-handling-design.md section 4.
    void snapToTarget() noexcept { currentPitchLog2Hz = targetPitchLog2Hz; }

    // Time constant, so it is NOT smoothed by the caller - same treatment as
    // the ADSR times and lfoRateHz. Changing it alters the rate of future
    // samples only, never the current position.
    void setGlideTimeSeconds (float seconds) noexcept { glideTimeSeconds = seconds; }

    float processSample() noexcept
    {
        // Exactly zero means off/instant - not merely "fast", so the control
        // has a true detent at the bottom of its range.
        if (glideTimeSeconds <= 0.0f)
        {
            currentPitchLog2Hz = targetPitchLog2Hz;
            return currentPitchLog2Hz;
        }

        const auto octavesPerSample = 1.0f / (glideTimeSeconds * (float) sampleRate);
        const auto distance = targetPitchLog2Hz - currentPitchLog2Hz;

        // Snap when within one step, so the ramp terminates exactly on target
        // rather than oscillating around it forever.
        if (distance <= octavesPerSample && distance >= -octavesPerSample)
            currentPitchLog2Hz = targetPitchLog2Hz;
        else
            currentPitchLog2Hz += (distance > 0.0f ? octavesPerSample : -octavesPerSample);

        return currentPitchLog2Hz;
    }

private:
    double sampleRate = 0.0;
    float currentPitchLog2Hz = 0.0f;
    float targetPitchLog2Hz = 0.0f;

    // 0 = instant. Guarded against negative values in processSample rather
    // than clamped here, so the "exactly zero is off" detent stays exact.
    float glideTimeSeconds = 0.0f;
};
