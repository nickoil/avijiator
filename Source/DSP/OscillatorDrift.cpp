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

#include "OscillatorDrift.h"

#if JUCE_DEBUG

void runOscillatorDriftSelfTest()
{
    constexpr double sr = 48000.0;
    constexpr int numSamples = 480000; // 10 seconds - several wander time constants

    //==========================================================================
    // FINITE AND BOUNDED over a long render - the leaky integrator must never
    // run away, and the defensive clamp must never actually need to visibly
    // distort the walk (if it did, the walk's own statistics would be wrong).
    {
        OscillatorDrift drift (0x1234u);
        drift.prepare (sr);

        auto maxAbs = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto value = drift.processSample();
            jassert (std::isfinite (value));
            maxAbs = juce::jmax (maxAbs, std::abs (value));
        }

        jassert (maxAbs <= 2.0f);   // the hard clamp
        jassert (maxAbs > 0.05f);   // sanity - it actually moved, not stuck at 0
    }

    //==========================================================================
    // TWO INDEPENDENTLY-SEEDED INSTANCES DIVERGE - the whole reason main and
    // sub each get their own seed (character-and-vim.md A3: "independent per
    // oscillator... so they drift against each other"). Same-seed instances
    // would produce the identical sequence, which would defeat the point
    // silently rather than failing loudly - checked here instead.
    {
        OscillatorDrift driftA (0x1234u);
        OscillatorDrift driftB (0x5678u);
        driftA.prepare (sr);
        driftB.prepare (sr);

        auto everDiverged = false;

        for (int i = 0; i < 1000; ++i)
            if (std::abs (driftA.processSample() - driftB.processSample()) > 1.0e-6f)
                everDiverged = true;

        jassert (everDiverged);
    }
}

#endif
