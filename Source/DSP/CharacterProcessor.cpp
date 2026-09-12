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

#include "CharacterProcessor.h"

#if JUCE_DEBUG

#include <array>

void runCharacterProcessorSelfTest()
{
    //==========================================================================
    // curvedVelocity: endpoints exact, monotonic in between, never exceeds
    // its input (a convex curve on [0,1] with matching endpoints stays below
    // the identity line everywhere in between).
    {
        jassert (curvedVelocity (0.0f) == 0.0f);
        jassert (curvedVelocity (1.0f) == 1.0f);

        auto previous = 0.0f;
        for (int i = 1; i <= 20; ++i)
        {
            const auto v = (float) i / 20.0f;
            const auto curved = curvedVelocity (v);
            jassert (curved >= previous);   // monotonic
            jassert (curved <= v + 1.0e-6f); // convex, at or below identity
            previous = curved;
        }
    }

    //==========================================================================
    // CharacterProcessor: disabled (the default) is an EXACT pass-through -
    // not approximately, exactly, over a real signal.
    {
        CharacterProcessor proc;
        proc.prepare (48000.0);

        for (int i = 0; i < 1000; ++i)
        {
            const auto input = std::sin ((float) i * 0.037f) * 0.6f;
            jassert (proc.processSample (input) == input);
        }
    }

    //==========================================================================
    // ENABLED: bounded, finite, and audibly different from a silent input -
    // the noise floor must actually be reaching the output even when the
    // input itself is dead silence.
    {
        CharacterProcessor proc;
        proc.prepare (48000.0);
        proc.setEnabled (true);

        auto everNonZero = false;
        auto maxAbs = 0.0f;

        for (int i = 0; i < 4800; ++i) // 0.1s
        {
            const auto output = proc.processSample (0.0f); // dead silence in
            jassert (std::isfinite (output));

            if (output != 0.0f)
                everNonZero = true;

            maxAbs = juce::jmax (maxAbs, std::abs (output));
        }

        jassert (everNonZero);      // the noise floor is live
        jassert (maxAbs < 0.01f);   // and it is genuinely QUIET - nowhere near audio level
    }

    //==========================================================================
    // ENABLED, a loud signal: bounded (the saturator actually engages) and
    // asymmetric (a symmetric input's positive and negative peaks are no
    // longer mirror images once saturation is asymmetric).
    {
        CharacterProcessor proc;
        proc.prepare (48000.0);
        proc.setEnabled (true);

        auto maxPositive = 0.0f;
        auto maxNegative = 0.0f;

        for (int i = 0; i < 4800; ++i)
        {
            const auto input = std::sin ((float) i * 0.05f) * 3.0f; // well past the linear region
            const auto output = proc.processSample (input);
            jassert (std::isfinite (output));
            jassert (std::abs (output) < 2.0f); // bounded, well clear of the un-clipped 3.0 peak

            maxPositive = juce::jmax (maxPositive, output);
            maxNegative = juce::jmin (maxNegative, output);
        }

        jassert (maxPositive > 0.01f && maxNegative < -0.01f); // sanity - both halves actually swung
        jassert (std::abs (maxPositive + maxNegative) > 1.0e-3f); // NOT symmetric
    }
}

#endif
