#include "Chorus.h"

#if JUCE_DEBUG

#include <cmath>

void runChorusSelfTest()
{
    Chorus chorus;
    chorus.prepare (48000.0);

    constexpr int numSamples = 4800; // 0.1s - several LFO phase steps at 0.6Hz

    auto everFinite = true;
    auto everDiverged = false;
    auto maxAbs = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // A steady tone, not silence - a silent input would make left/right
        // trivially identical (both zero) regardless of whether the taps are
        // actually reading different delay positions.
        const auto input = std::sin ((float) i * 0.05f);

        float left = 0.0f, right = 0.0f;
        chorus.processSample (input, left, right);

        if (! (std::isfinite (left) && std::isfinite (right)))
            everFinite = false;

        maxAbs = juce::jmax (maxAbs, std::abs (left), std::abs (right));

        if (std::abs (left - right) > 1.0e-4f)
            everDiverged = true;
    }

    jassert (everFinite);
    jassert (maxAbs < 4.0f); // bounded - a delay line with a fixed 0.5 wet mix cannot run away
    jassert (everDiverged); // left and right are genuinely independent, not a mono copy
}

#endif
