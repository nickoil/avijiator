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

    Curve is linear by default - see documents/envelope-lfo-design.md
    section 1 - with an optional exponential Decay/Release shape gated by
    setCurveEnabled (character-and-vim.md A2, Tier 1 "Vim"). Attack stays
    linear either way; the doc's v1 scope only asks for Decay/Release, and
    only those two ever have a level to asymptotically approach rather than
    a fixed 0/1 target - see processSample's Decay/Release cases.

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

    // character-and-vim.md A2 (Tier 1 "Vim"). Discrete switch, not smoothed -
    // same treatment as every other on/off in this codebase (SynthVoice reads
    // VoiceParameters::vimEnabled raw once per block, same as
    // envelopeDestination/lfoWaveform). false (the default) takes the ORIGINAL
    // linear branch in processSample's Decay/Release cases, unchanged - see
    // that method's own comment for why the two modes are byte-identical at
    // false rather than merely similar.
    void setCurveEnabled (bool shouldBeExponential) noexcept { curveEnabled = shouldBeExponential; }

    Stage getStage() const noexcept { return stage; }

    // Advances one sample, returns the current level, 0..1.
    float processSample() noexcept;

private:
    // Guards against a divide-by-zero if a time is set to (or defaults to) 0.
    static constexpr float minStageSeconds = 0.001f;

    // character-and-vim.md A2. The exponential branch models the stage time
    // as a TIME CONSTANT rather than a completion time - a true exponential
    // asymptote never actually reaches its target - and this fraction is what
    // ties it back to something that reads like the same knob: 5 time
    // constants is ~99.3% of the way there, so a curve set to the same
    // seconds value as the linear ramp still lands close to target by
    // roughly the same moment, while keeping the fast-initial/slower-tail
    // shape the doc asks for. BY EAR, not derived - same posture as every
    // other shaping constant in this codebase (Vcf's maxFeedback, etc).
    static constexpr float exponentialTauFraction = 1.0f / 5.0f;

    // How close counts as "arrived", so the exponential branch can hand off
    // to Sustain/Idle instead of asymptoting forever. Small enough to be
    // inaudible against the linear branch's own exact arrival.
    static constexpr float exponentialSnapEpsilon = 0.0005f;

    double sampleRate = 0.0;
    Stage stage = Stage::Idle;
    float currentLevel = 0.0f;

    float attackSeconds = 0.01f;
    float decaySeconds = 0.1f;
    float sustainLevel = 0.7f;
    float releaseSeconds = 0.3f;

    bool curveEnabled = false;
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    Covers character-and-vim.md A2: setCurveEnabled's two branches in
    Decay/Release. Proves, in order: the OFF path (the default, and the only
    path that existed before A2) is byte-identical to the exact linear
    arithmetic it always was - not just "still linear-looking" but the same
    numbers, sample for sample; the ON path actually reaches its target
    (sustainLevel for Decay, 0 for Release) and hands off to the next stage,
    rather than asymptoting forever; and the two paths diverge partway
    through a stage, so a future refactor that quietly ties curveEnabled off
    from actually mattering would fail here rather than only being noticed
    by ear.
*/
void runAdsrCurveSelfTest();

#endif
