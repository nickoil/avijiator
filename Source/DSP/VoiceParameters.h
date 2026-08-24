#pragma once

#include <atomic>

#include "Lfo.h"

//==============================================================================
/*
    Where the shared envelope's output is routed - CLAUDE.md's hard constraint
    is ONE envelope, switchable between destinations, not a second envelope.
    Stored as a plain std::atomic<int>, no smoother: it's a discrete switch,
    not a continuous value - see documents/envelope-lfo-design.md section 5.
*/
enum class EnvelopeDestination : int { Filter = 0, Amp = 1, Both = 2 };

//==============================================================================
/*
    Every UI -> audio parameter for the voice lives here as an atomic, per
    CLAUDE.md: UI -> audio changes go through atomics, never a shared mutable
    field. SynthVoice snapshots these once per block; continuous values are
    then smoothed per sample, discrete switches and time constants are read
    raw - see documents/dsp-voice-design.md section 4 and
    documents/envelope-lfo-design.md section 5 for which is which and why.

    Grew incrementally as each build step landed its oscillator/filter tap
    (item 2, documents/dsp-voice-design.md section 7) and then its envelope/
    LFO parameter (item 3, documents/envelope-lfo-design.md section 7). Item
    4 (note handling) is next to add to this struct.
*/
struct VoiceParameters
{
    // Pitch is stored as log2(Hz), not Hz. Two payoffs: modulation sums
    // linearly in octaves, and linear smoothing of a log2 value is
    // multiplicative smoothing of the frequency - which is what a frequency
    // sweep has to be to avoid zipper noise. Default is log2(87.31) = F2.
    std::atomic<float> pitchLog2Hz { 6.4483f };
    std::atomic<float> sawLevel { 0.70f };
    std::atomic<float> pulseLevel { 0.00f };

    // Duty cycle, 0..1. The oscillator clamps this against the current phase
    // increment, so the usable range narrows toward 0.5 at high pitch.
    std::atomic<float> pulseWidth { 0.50f };

    std::atomic<float> subLevel { 0.00f };
    std::atomic<float> noiseLevel { 0.00f };

    // Also log2(Hz), for the same reasons as pitch - and so envelope and LFO
    // modulation can sum in octaves, which is the only way a modulator sounds
    // the same at 200 Hz as at 5 kHz. Default is log2(2000).
    std::atomic<float> cutoffLog2Hz { 10.9658f };

    // 0..1, mapped onto the filter's feedback gain. Self-oscillates near the
    // top of the range.
    std::atomic<float> resonance { 0.20f };

    std::atomic<float> outputLevel { 0.25f };

    // Item 3 (envelope + LFO). No real note input yet (that's item 4) - this
    // is a temporary manual gate for testing, written from the message thread
    // by a debug button. SynthVoice edge-detects it on the audio thread
    // rather than acting on it directly here - see
    // documents/envelope-lfo-design.md section 2.
    std::atomic<bool> gate { false };

    // Envelope times: NOT smoothed by SynthVoice (see envelope-lfo-design.md
    // section 5) - changing one only affects the rate of future samples, not
    // the current output value, so there's no click to smooth away.
    std::atomic<float> attackSeconds { 0.01f };
    std::atomic<float> decaySeconds { 0.1f };
    std::atomic<float> releaseSeconds { 0.3f };

    // Directly assigned as the envelope's output level during Sustain, so
    // (unlike the times above) this one IS smoothed - an unsmoothed jump here
    // would be a real, audible pop.
    std::atomic<float> sustainLevel { 0.7f };

    // Default is Amp: the safest first-load behaviour - the voice goes
    // silent when not gated, rather than continuing to drone like item 2
    // left it. Starting pick, easy to change by ear.
    std::atomic<int> envelopeDestination { (int) EnvelopeDestination::Amp };

    // Octaves. Directly scales a modulation amount every sample, so - unlike
    // the envelope times above - this IS smoothed: a depth jump would be an
    // audible pop.
    std::atomic<float> envToCutoffDepthOctaves { 0.0f };

    // Hz. NOT smoothed - same time-constant reasoning as the envelope times
    // above: changing the rate only affects the LFO's future phase
    // increment, not its current output value.
    std::atomic<float> lfoRateHz { 2.0f };

    // Discrete, no smoother - matches envelopeDestination's treatment.
    std::atomic<int> lfoWaveform { (int) Lfo::Waveform::Triangle };

    // Octaves. Directly scale a modulation amount every sample, so - like
    // envToCutoffDepthOctaves above - these ARE smoothed.
    std::atomic<float> lfoToPitchDepthOctaves { 0.0f };
    std::atomic<float> lfoToCutoffDepthOctaves { 0.0f };

    static_assert (std::atomic<float>::is_always_lock_free,
                   "Parameter stores must not take a lock on the message thread "
                   "or block the audio thread reading them.");
    static_assert (std::atomic<bool>::is_always_lock_free,
                   "Same requirement as the float parameters above.");
};
