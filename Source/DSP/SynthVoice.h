#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "VoiceParameters.h"
#include "Vca.h"

//==============================================================================
/*
    The voice core: no MIDI, no timers, no tempo, no juce::AudioBuffer - takes
    numbers, produces samples. This is the reuse boundary shared by the arp
    (item 5) and step sequencer (item 7) front ends - see
    documents/dsp-voice-design.md section 6.

    Step 1: a naive (deliberately aliased) saw at a fixed test pitch, straight
    into the VCA. No filter yet (arrives item 5/step 5), no real oscillator
    yet (arrives item 2/step 2) - this naive saw is the "before" reference for
    the aliasing A/B that step is meant to pass.
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

    // Placeholder pitch until item 2's real oscillator + Pitch slider land.
    static constexpr double testOscillatorFrequencyHz = 110.0;
    static constexpr double rampSeconds = 0.02;

    VoiceParameters parameters;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputLevelSmoothed;

    double sampleRate = 0.0;
    double phase = 0.0;
    double phaseIncrement = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthVoice)
};
