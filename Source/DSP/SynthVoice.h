#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Adsr.h"
#include "Glide.h"
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

    Item 4 (note handling): pitch is no longer a UI parameter - it arrives as
    note events and is owned by Glide, with legato/retrigger deciding whether
    an overlapping note restarts the envelope. See
    documents/note-handling-design.md.
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

    //==============================================================================
    // Note control. AUDIO THREAD ONLY - NoteRouter calls these after draining
    // its event FIFOs at the top of a block. Never call them from the message
    // thread; that is what the FIFOs are for.
    //
    // Three methods, not two, and the third is load-bearing: noteOn() cannot
    // tell "a key went down" from "a key came up, revealing another still
    // held" by itself, and conflating those would make Retrigger mode
    // re-pluck on every note-off inside a chord. See
    // documents/note-handling-design.md section 5.

    // A key went down. Legato/retrigger mode decides whether this restarts
    // the envelope, but only when a note was already sounding.
    void noteOn (float pitchLog2Hz, float velocity) noexcept;

    // A key came up and another is still held. Pitch moves; the envelope is
    // never restarted, in either mode.
    void retargetPitch (float pitchLog2Hz) noexcept;

    // Every key is now up.
    void noteOff() noexcept;

private:
    void snapshotParameters (bool jumpImmediately) noexcept;

    // Audio-thread-private. Distinguishes "a note is already sounding" from
    // silence, which is the overlap signal legato/retrigger keys off.
    bool voiceGated = false;

    // Captured but not routed anywhere yet - item 7's accent and
    // character-and-vim.md B5's velocity routing are the eventual consumers.
    // Declared now for the same reason item 2 pre-declared its modulation
    // summing points at zero: so filling it in later is an edit, not a
    // restructure.
    float currentVelocity = 0.0f;

    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    static constexpr double rampSeconds = 0.02;

    // A resonance jump moves the whole feedback loop at once and thumps, so
    // it gets a deliberately slower ramp than everything else.
    static constexpr double resonanceRampSeconds = 0.05;

    // log2(261.63) - middle C. Only a defined resting value for the glide
    // ramp before any note has ever played; the first note-on snaps away from
    // it, so it is never heard.
    static constexpr float defaultPitchLog2Hz = 8.0313f;

    VoiceParameters parameters;
    PolyBlepOscillator oscillator;
    NoiseGenerator noise;
    Vcf filter;
    Adsr envelope;
    Lfo lfo;

    // Owns the base pitch outright - this REPLACED item 3's pitchLog2Smoothed
    // rather than layering on top of it. That smoother was a 20ms anti-zipper
    // ramp on a debug slider; glide IS the note-to-note pitch transition, so
    // modelling it as a second additive ramp would mean implementing the same
    // thing twice. See documents/note-handling-design.md section 4.
    Glide glide;

    // Linear smoothing on a log2(Hz) value IS multiplicative smoothing of the
    // frequency, which is the musically correct sweep - and it sidesteps
    // ValueSmoothingTypes::Multiplicative's strictly-positive constraint. One
    // smoother type everywhere.
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
