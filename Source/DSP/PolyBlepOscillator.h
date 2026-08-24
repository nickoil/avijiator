#pragma once

//==============================================================================
/*
    One phase accumulator, several taps — mirrors a real SH-101 VCO, where saw,
    pulse and the sub-divider all hang off a single oscillator. Phase advances
    exactly once per sample and every tap reads that same phase, so the taps
    cannot drift apart.

    Full PolyBLEP derivation in documents/dsp-voice-design.md section 2.

    All three taps present: saw, pulse/PWM, and the sub-oscillator one octave
    down. Noise is a separate generator (NoiseGenerator.h), not a tap off this
    phase.
*/
class PolyBlepOscillator
{
public:
    struct Frame
    {
        float saw = 0.0f;
        float pulse = 0.0f;
        float sub = 0.0f;
    };

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    void setFrequency (float frequencyHz) noexcept;
    void setPulseWidth (float newPulseWidth) noexcept;

    Frame processSample() noexcept;

private:
    //==============================================================================
    // Band-limited step residual, scaled for a discontinuity of amplitude 2 —
    // which is exactly the saw's wrap (+1 -> -1) and also the pulse's edges,
    // so it is used unscaled for both.
    static float polyBlep (float t, float dt) noexcept;

    static constexpr float minFrequencyHz = 8.0f;

    // fs/4 rather than Nyquist: the sub-oscillator runs at half this
    // increment, and each BLEP correction window needs dt of room either side
    // of its edge. It also guarantees the duty clamp in processSample can
    // never invert - see the comment there.
    static constexpr double maxIncrement = 0.25;

    static constexpr float minPulseWidth = 0.02f;
    static constexpr float maxPulseWidth = 0.98f;

    double inverseSampleRate = 0.0;
    double phase = 0.0;
    double phaseIncrement = 0.0;

    float pulseWidth = 0.5f;

    // Divide-by-two flip-flop for the sub-oscillator, toggled on each main
    // phase wrap. The sub's phase is derived from this plus the main phase,
    // never accumulated separately - see processSample.
    bool subHigh = false;
};
