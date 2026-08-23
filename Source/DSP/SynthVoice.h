#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "PolyBlepOscillator.h"
#include "VoiceParameters.h"
#include "Vca.h"

//==============================================================================
/*
    The voice core: no MIDI, no timers, no tempo, no juce::AudioBuffer — takes
    numbers, produces samples. This is the reuse boundary shared by the arp
    (item 5) and step sequencer (item 7) front ends — see
    documents/dsp-voice-design.md section 6.

    Step 2 (Saw): band-limited saw -> VCA. No pulse/sub/noise yet (steps 3-4),
    no filter yet (steps 5-6).
*/
class SynthVoice
{
public:
    SynthVoice() = default;

    void prepare (double newSampleRate);
    void reset() noexcept;

    // Writes into output, does not add to it. Raw-pointer + count rather than
    // an AudioSourceChannelInfo/AudioBuffer so item 5's sample-accurate arp
    // clock can call this several times per block with advancing pointers.
    void renderNextBlock (float* output, int numSamples) noexcept;

    VoiceParameters& getParameters() noexcept { return parameters; }

private:
    void snapshotParameters (bool jumpImmediately) noexcept;

    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    static constexpr double rampSeconds = 0.02;

    VoiceParameters parameters;
    PolyBlepOscillator oscillator;

    // Linear smoothing on a log2(Hz) value IS multiplicative smoothing of the
    // frequency, which is the musically correct sweep - and it sidesteps
    // ValueSmoothingTypes::Multiplicative's strictly-positive constraint. One
    // smoother type everywhere.
    Smoothed pitchLog2Smoothed;
    Smoothed sawLevelSmoothed;
    Smoothed outputLevelSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthVoice)
};
