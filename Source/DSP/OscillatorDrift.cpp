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
