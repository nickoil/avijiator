#pragma once

//==============================================================================
/*
    Hand-rolled ADSR envelope generator. Hand-rolled for the same reason as the
    oscillator and filter: full control, and no need to verify juce::ADSR's
    exact retrigger/thread-safety behaviour by reading library sources.

    Rate-calibrated-to-full-range convention: each stage's increment is sized
    to cross the ENTIRE 0..1 range in the stage's nominal time, not the actual
    gap it's crossing. Retriggering Attack partway up (from noteOn() while
    still decaying) proportionally finishes faster - click-free by
    construction, nothing special-cased for it.

    Curve is deliberately linear, not exponential - see
    documents/envelope-lfo-design.md section 1. Exponential curves are a later
    Tier-1 "Vim" addition (character-and-vim.md A2), not part of this clean
    core.

    Full derivation in documents/envelope-lfo-design.md section 2.
*/
class Adsr
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    // -> Attack / Release, from wherever currentLevel currently is. Calling
    // noteOff() while Idle is a no-op - nothing to release.
    void noteOn() noexcept;
    void noteOff() noexcept;

    // Time constants - not smoothed by the caller (see the parameter-plumbing
    // table in envelope-lfo-design.md section 5): changing these only affects
    // the rate of future samples, not the current output value, so there's no
    // click to smooth away.
    void setAttackSeconds (float seconds) noexcept  { attackSeconds = seconds; }
    void setDecaySeconds (float seconds) noexcept   { decaySeconds = seconds; }
    void setReleaseSeconds (float seconds) noexcept { releaseSeconds = seconds; }

    // Directly assigned as currentLevel during Sustain - the caller smooths
    // this one (unlike the times above) because an unsmoothed jump here would
    // be a real, audible pop.
    void setSustainLevel (float level) noexcept { sustainLevel = level; }

    Stage getStage() const noexcept { return stage; }

    // Advances one sample, returns the current level, 0..1.
    float processSample() noexcept;

private:
    // Guards against a divide-by-zero if a time is set to (or defaults to) 0.
    static constexpr float minStageSeconds = 0.001f;

    double sampleRate = 0.0;
    Stage stage = Stage::Idle;
    float currentLevel = 0.0f;

    float attackSeconds = 0.01f;
    float decaySeconds = 0.1f;
    float sustainLevel = 0.7f;
    float releaseSeconds = 0.3f;
};
