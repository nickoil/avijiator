#pragma once

#include <cstdint>

//==============================================================================
/*
    White noise via xorshift32. Hand-rolled rather than juce::Random for two
    reasons: it is a handful of branchless integer ops, and the sequence is
    repeatable from a fixed seed - which makes an A/B between two builds an
    actual comparison rather than two different noise records.
*/
class NoiseGenerator
{
public:
    // Seed is settable so two generators in the same signal path (the audible
    // noise source and the filter's self-oscillation floor) do not emit
    // identical, correlated sequences.
    explicit NoiseGenerator (std::uint32_t seed = defaultSeed) noexcept
        : state (seed), initialSeed (seed)
    {
    }

    void reset() noexcept { state = initialSeed; }

    float processSample() noexcept
    {
        // xorshift32, Marsaglia's constants.
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        // Top 24 bits mapped to [-1, 1). The high bits are used rather than
        // the low ones because xorshift's low bits are the weaker of the two.
        return (float) (std::int32_t) (state >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }

private:
    static constexpr std::uint32_t defaultSeed = 0x9e3779b9u; // any non-zero value

    std::uint32_t state = defaultSeed;
    std::uint32_t initialSeed = defaultSeed;
};
