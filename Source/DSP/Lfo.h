#pragma once

#include "NoiseGenerator.h"

//==============================================================================
/*
    Triangle / square / sample-and-hold LFO. One phase accumulator, same style
    as the audio oscillator, but no PolyBLEP - LFO rates are sub-audio, so
    aliasing doesn't apply at the ranges this covers.

    Sample-and-hold: draws a new random value from its own seeded
    NoiseGenerator exactly at each phase wrap, holds it constant until the
    next one - classic stepped/glitchy character, not a smoothed random walk.

    Full derivation in documents/envelope-lfo-design.md section 3.
*/
class Lfo
{
public:
    enum class Waveform { Triangle, Square, SampleAndHold };

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    void setRate (float rateHz) noexcept;
    void setWaveform (Waveform newWaveform) noexcept { waveform = newWaveform; }

    // Bipolar, -1..1.
    float processSample() noexcept;

private:
    double inverseSampleRate = 0.0;
    double phase = 0.0;
    double phaseIncrement = 0.0;

    Waveform waveform = Waveform::Triangle;

    // Distinct seed from the voice's audible noise source and the filter's
    // floor noise, so none of the three noise streams are correlated.
    NoiseGenerator sampleAndHoldNoise { 0x27d4eb2fu };
    float heldRandomValue = 0.0f;
};
