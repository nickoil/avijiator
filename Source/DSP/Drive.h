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

//==============================================================================
/*
    character-and-vim.md A1, REVISED THREE TIMES (item 10). The original
    build put the drive stage INSIDE Vcf's resonance feedback loop - the user
    found it pointless, it only ever coloured cutoff/resonance interaction,
    never read as real distortion. The second attempt moved it to a plain
    PRE-filter stage but kept the OLD design's makeup-gain instinct
    (crossfading toward a fixed-hardness shaper rather than actually raising
    gain) - the user's second piece of feedback: that still wasn't a
    distortion-pedal character, because the level stayed roughly constant
    instead of climbing with drive. That was fixed (real gain-into-clip, no
    makeup gain), but the user's THIRD piece of feedback caught a placement
    bug that math alone couldn't: pre-filter, the VCF running right after
    Drive could remove exactly the harmonics Drive had just added whenever
    cutoff was not wide open - reported as sounding CLEANER, not dirtier,
    once engaged.

    THIS version fixes both: a real gain-into-clip stage (driveAmount raises
    the INPUT gain into a tanh clipper, result used as-is, no makeup gain -
    the signal gets LOUDER and DIRTIER together, the actual trade a real
    overdrive/distortion pedal makes), applied by SynthVoice AFTER the filter
    rather than before it - "a pedal at the output", not "drive into the
    filter". Post-filter, whatever the VCF decided to pass always gets
    driven, independent of cutoff/resonance position. Vcf.cpp itself is
    unaffected by any of this - it is back to exactly what it was before A1
    first touched it (softClip is a stability requirement again, not
    flavour).

    Stateless, like Vca - the caller owns the smoothed drive amount (see
    SynthVoice), this just shapes.

    EXACTLY driveAmount == 0 is still an explicit bypass (not merely "close
    to identity"), same "0 = inert" discipline every other depth knob in
    this codebase holds to - see processSample's own early return. Above 0
    the transfer function is NOT required to stay smooth as the knob first
    moves - a real drive pedal's low end is not silky either; the audible
    goal here is "off is truly off, on has real character", not
    continuity at the boundary.
*/
struct Drive
{
    static float processSample (float input, float driveAmount) noexcept
    {
        if (driveAmount <= 0.0f)
            return input;

        // BY EAR, not derived - same posture as every other shaping constant
        // in this codebase. NO division back down by driveGain - that
        // division is what would have made this "grittier at constant
        // level" again; leaving it out is the entire point of this
        // revision. tanh's own range (-1, 1) is the only bound this needs -
        // Vcf's own softClip handles whatever level reaches it next.
        constexpr float maxDriveGain = 10.0f;

        const auto driveGain = 1.0f + driveAmount * (maxDriveGain - 1.0f);
        return std::tanh (input * driveGain);
    }
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    Proves the two properties that matter for THIS revision specifically:
    exact bypass at driveAmount == 0 (still non-negotiable), and that turning
    drive up makes a signal LOUDER as well as different-shaped - not merely
    "different", the specific regression the user's feedback flagged in the
    previous build.
*/
void runDriveSelfTest();

#endif
