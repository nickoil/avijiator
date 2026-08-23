#pragma once

//==============================================================================
/*
    One phase accumulator, several taps — mirrors a real SH-101 VCO, where saw,
    pulse and the sub-divider all hang off a single oscillator. Phase advances
    exactly once per sample and every tap reads that same phase, so the taps
    cannot drift apart.

    Full PolyBLEP derivation in documents/dsp-voice-design.md section 2.

    Step 2 (Saw): the saw tap only. Pulse + PWM join at step 3 and the sub at
    step 4 — Frame gains fields and processSample gains lines, but the phase
    accumulator does not change.
*/
class PolyBlepOscillator
{
public:
    struct Frame
    {
        float saw = 0.0f;
    };

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    void setFrequency (float frequencyHz) noexcept;

    Frame processSample() noexcept;

private:
    //==============================================================================
    // Band-limited step residual, scaled for a discontinuity of amplitude 2 —
    // which is exactly the saw's wrap (+1 -> -1), and also the pulse's edges
    // when they arrive at step 3, so it is used unscaled for both.
    static float polyBlep (float t, float dt) noexcept;

    static constexpr float minFrequencyHz = 8.0f;

    // fs/4 rather than Nyquist: the sub-oscillator (step 4) runs at half this
    // increment, and each BLEP correction window needs dt of room either side
    // of its edge.
    static constexpr double maxIncrement = 0.25;

    double inverseSampleRate = 0.0;
    double phase = 0.0;
    double phaseIncrement = 0.0;
};
