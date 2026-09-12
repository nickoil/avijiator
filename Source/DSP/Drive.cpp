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

#include "Drive.h"

#if JUCE_DEBUG

#include <cmath>

void runDriveSelfTest()
{
    //==========================================================================
    // EXACT BYPASS at driveAmount == 0 - an explicit early return, not an
    // arithmetic coincidence, so this holds regardless of input level
    // (including well past unity, where the clipper would otherwise already
    // be shaping the signal).
    for (int i = -50; i <= 50; ++i)
    {
        const auto x = (float) i * 0.05f; // -2.5 .. 2.5
        jassert (Drive::processSample (x, 0.0f) == x);
    }

    //==========================================================================
    // FINITE AND BOUNDED once driven - tanh's own range.
    {
        for (int i = -50; i <= 50; ++i)
        {
            const auto x = (float) i * 0.05f;
            const auto driven = Drive::processSample (x, 1.0f);
            jassert (std::isfinite (driven));

            // tanh's range is the OPEN interval (-1, 1), but float32 rounds
            // a large enough argument to exactly 1.0f (no representable
            // value is closer to 1 than that) - so the bound checked here is
            // <=, not <, or this would fail on the exact inputs it exists to
            // prove are safe.
            jassert (std::abs (driven) <= 1.0f);
        }
    }

    //==========================================================================
    // LOUDER AS WELL AS DIRTIER - the specific property this revision exists
    // for. A real (quiet) signal's PEAK level must rise as driveAmount rises,
    // not stay roughly constant - the previous build's regression, caught
    // here rather than by ear a second time. A small, sub-unity sine so
    // there is real headroom to climb into before tanh's own ceiling limits
    // it.
    {
        const auto peakAt = [] (float driveAmount)
        {
            auto peak = 0.0f;

            for (int i = 0; i < 200; ++i)
            {
                const auto x = 0.15f * std::sin ((float) i * 0.13f);
                peak = juce::jmax (peak, std::abs (Drive::processSample (x, driveAmount)));
            }

            return peak;
        };

        const auto peakOff = peakAt (0.0f);
        const auto peakLow = peakAt (0.3f);
        const auto peakHigh = peakAt (1.0f);

        jassert (peakOff > 0.01f); // sanity - the reference signal is actually sounding
        jassert (peakLow > peakOff);   // louder as soon as it's driven at all
        jassert (peakHigh > peakLow);  // louder still at full drive
    }
}

#endif
