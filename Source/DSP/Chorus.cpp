#include "Chorus.h"

#if JUCE_DEBUG

#include <cmath>

void runChorusSelfTest()
{
    Chorus chorus;
    chorus.prepare (48000.0);

    // 4 seconds - several FULL LFO cycles (period ~1.67s at 0.6Hz), not just a
    // few phase steps. Load-bearing: the original 0.1s version of this test
    // never once let the read position cross zero, so it never reached the
    // exact floating-point rounding edge case (readDelayed's own comment)
    // that crashed in real use - caught by the user hitting it while playing,
    // not by this self-test, until the duration was fixed here.
    constexpr int numSamples = 192000;

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
