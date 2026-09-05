#pragma once

// So JUCE_DEBUG is defined before the #if JUCE_DEBUG block at the bottom of
// this header regardless of what a given translation unit has included
// before this header - same self-contained pattern StepClock.h uses.
#include <juce_core/juce_core.h>

#include "NoiseGenerator.h"
#include "TptSvfStage.h"

//==============================================================================
/*
    24dB resonant lowpass: two 12dB TPT SVF stages in cascade.

    Each stage runs at FIXED critical damping (k = 2), which makes one stage
    1/(s+1)^2 and the cascade 1/(s+1)^4 - four coincident real poles, the same
    pole placement a four-pole ladder has, built from stages we own.

    Resonance deliberately does NOT live in the per-stage damping. It is a
    single global feedback path around the whole cascade, solved zero-delay -
    which gives an exactly derivable self-oscillation threshold.

    Evaluating the cascade at the cutoff (s = j) gives (j+1)^4 = -4, i.e.
    magnitude 1/4 at exactly 180 degrees - so negative feedback becomes
    POSITIVE at the cutoff, and unity loop gain (self-oscillation) lands at
    k = 4. That is the same reason a Moog ladder's resonance runs 0..4.

    A linear filter does not self-oscillate at a steady amplitude above that
    threshold - it diverges to inf/NaN, since nothing bounds the loop. softClip
    on the feedback path (below) is what makes k > 4 usable at all: it is a
    STABILITY requirement, found by ear when full resonance killed the voice
    with no recovery - it engages only once the loop is already running away,
    not for flavour.

    character-and-vim.md A1 (the former "drive stage" TODO this class used to
    describe as still open) is the flavour version: driveSaturate below pushes
    the SAME feedback-loop signal harder into the SAME softClip nonlinearity,
    scaled by a caller-supplied 0..1 amount, so cutoff/resonance interaction
    turns nonlinear and self-oscillation turns more musical at normal signal
    levels too - not just once resonance pushes the loop past its own
    stability threshold. Placement (inside the loop, not on the output) is
    the whole point - see A1's own note in that doc.

    See documents/dsp-voice-design.md section 3.
*/
class Vcf
{
public:
    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    // Takes cutoff in log2(Hz) - the caller sums its modulation in the octave
    // domain and exp2 happens in here, once, after that sum. Resonance is
    // 0..1, mapped onto the feedback gain internally. driveAmount is 0..1
    // (character-and-vim.md A1) - see the doc comment on driveSaturate below
    // for what it does and why it is byte-identical at 0.
    float processSample (float input, float cutoffLog2Hz, float resonance01, float driveAmount) noexcept;

private:
    TptSvfCoefficients makeCoefficients (float cutoffHz) const noexcept;

    //==============================================================================
    // Bounds the feedback loop. NOT flavour - this is a stability requirement.
    // Above k = 4 the linear loop gain exceeds unity, so a purely linear
    // filter does not self-oscillate at a steady amplitude, it grows without
    // bound until it reaches inf and then NaN, and NaN states latch forever.
    // Real analogue is bounded by transistor saturation; this is the
    // equivalent.
    //
    // Deliberately EXACTLY linear below the threshold so the signal stays
    // vanilla at normal levels - it only engages when the loop is running
    // away. The voiced drive/saturation TODO is a different, larger thing:
    // a driven stage intended to colour the sound at all levels.
    static float softClip (float x) noexcept;

    // character-and-vim.md A1. Pushes x harder into softClip's SAME
    // nonlinearity by a caller-supplied gain, then pulls that gain back out
    // afterwards - so the shape saturates more as driveAmount rises, but the
    // overall level does not simply get louder. At driveAmount == 0,
    // driveGain is exactly 1.0f, so this is `softClip (x) / 1.0f` -
    // byte-identical to calling softClip(x) directly. See processSample's
    // own comment for where this sits in the feedback loop.
    static float driveSaturate (float x, float driveAmount) noexcept;

    static constexpr float softClipThreshold = 1.0f;

    static constexpr float stageDamping = 2.0f;

    static constexpr float minCutoffHz = 20.0f;
    static constexpr float maxCutoffHz = 18000.0f;

    //==============================================================================
    // BY EAR, not derived - see documents/dsp-voice-design.md section 8.

    // Unity loop gain is exactly 4.0; the overshoot puts the top of the knob
    // firmly on the oscillating side rather than sitting on the threshold.
    static constexpr float maxFeedback = 4.5f;

    // Feedback drops DC gain to 1/(1+k). Analogue ladders lose bass the same
    // way, so only part of it is given back: 0 = full analogue bass loss,
    // 1 = level held flat, which sounds thin and clinical.
    static constexpr float resonanceCompensation = 0.5f;

    // BY EAR, not derived - same posture as maxFeedback/resonanceCompensation
    // above. driveSaturate's gain at driveAmount == 1: pushes the feedback
    // signal to 6x softClipThreshold before the makeup division, comfortably
    // into the tanh curve's compressed region without needing a second knob
    // to tame level. See character-and-vim.md A1.
    static constexpr float maxDriveGain = 6.0f;

    // -120 dBFS. A perfectly zero input into a perfectly zero state stays
    // zero forever, so self-oscillation would never start. Real analogue
    // starts from thermal noise; this is the equivalent. It also keeps the
    // integrator states above denormal range.
    static constexpr float noiseFloorAmplitude = 1.0e-6f;

    double sampleRate = 0.0;

    // tan() blows up approaching Nyquist, so the upper cutoff limit is also
    // capped at a fraction of the sample rate - see prepare().
    float upperCutoffHz = maxCutoffHz;

    TptSvfStage stage1;
    TptSvfStage stage2;

    // Distinct seed so this does not emit the same sequence as the voice's
    // audible noise source.
    NoiseGenerator floorNoise { 0x5bf03635u };
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    Covers character-and-vim.md A1: driveSaturate's presence inside
    processSample's feedback loop. Proves, in order: driveAmount == 0 renders
    byte-identical to a driveAmount that was never introduced at all - not
    approximately, exactly, sample for sample, over a real sweep through the
    filter rather than a single static input; and driveAmount turned up
    changes the output - the term is actually reaching the loop, not a dead
    parameter.
*/
void runVcfDriveSelfTest();

#endif
