#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Adsr.h"
#include "Lfo.h"
#include "NoiseGenerator.h"
#include "PolyBlepOscillator.h"
#include "VoiceParameters.h"
#include "Vca.h"
#include "Vcf.h"

//==============================================================================
/*
    The voice core: no MIDI, no timers, no tempo, no juce::AudioBuffer — takes
    numbers, produces samples. This is the reuse boundary shared by the arp
    (item 5) and step sequencer (item 7) front ends — see
    documents/dsp-voice-design.md section 6.

    Item 2 (oscillator + filter core): four sources - saw, pulse/PWM, sub,
    noise - each with an independent level, mixed and sent through a 24dB
    resonant lowpass to the VCA. Built incrementally per
    documents/dsp-voice-design.md section 7.

    Item 3 (envelope + LFO): one shared ADSR, routable to VCF/VCA/Both
    (CLAUDE.md's hard constraint - one envelope, never a second), plus a
    triangle/square/sample-and-hold LFO routable to pitch and/or cutoff
    independently. All three modulation summing points item 2 left at zero
    are filled in. Built incrementally per
    documents/envelope-lfo-design.md section 7.
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

    // Audio-thread-only record of the gate's last known state, so the edge
    // (not just the level) can be detected safely from inside
    // renderNextBlock. Never touched from the message thread - the button
    // only ever writes VoiceParameters::gate.
    bool lastGateState = false;

    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    static constexpr double rampSeconds = 0.02;

    // A resonance jump moves the whole feedback loop at once and thumps, so
    // it gets a deliberately slower ramp than everything else.
    static constexpr double resonanceRampSeconds = 0.05;

    VoiceParameters parameters;
    PolyBlepOscillator oscillator;
    NoiseGenerator noise;
    Vcf filter;
    Adsr envelope;
    Lfo lfo;

    // Linear smoothing on a log2(Hz) value IS multiplicative smoothing of the
    // frequency, which is the musically correct sweep - and it sidesteps
    // ValueSmoothingTypes::Multiplicative's strictly-positive constraint. One
    // smoother type everywhere.
    Smoothed pitchLog2Smoothed;
    Smoothed sawLevelSmoothed;
    Smoothed pulseLevelSmoothed;
    Smoothed pulseWidthSmoothed;
    Smoothed subLevelSmoothed;
    Smoothed noiseLevelSmoothed;
    Smoothed cutoffLog2Smoothed;
    Smoothed resonanceSmoothed;
    Smoothed outputLevelSmoothed;
    Smoothed sustainLevelSmoothed;
    Smoothed envToCutoffDepthSmoothed;
    Smoothed lfoToPitchDepthSmoothed;
    Smoothed lfoToCutoffDepthSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthVoice)
};
