#pragma once

#include <atomic>

//==============================================================================
/*
    Every UI -> audio parameter for the voice lives here as an atomic, per
    CLAUDE.md: UI -> audio changes go through atomics, never a shared mutable
    field. SynthVoice snapshots these once per block and smooths them per
    sample - see documents/dsp-voice-design.md section 4.

    Grows incrementally as each build step lands its oscillator/filter tap
    (documents/dsp-voice-design.md section 7). Pulse width, sub/noise levels,
    cutoff and resonance are added by the steps that introduce the thing they
    control.
*/
struct VoiceParameters
{
    // Pitch is stored as log2(Hz), not Hz. Two payoffs: modulation sums
    // linearly in octaves, and linear smoothing of a log2 value is
    // multiplicative smoothing of the frequency - which is what a frequency
    // sweep has to be to avoid zipper noise. Default is log2(87.31) = F2.
    std::atomic<float> pitchLog2Hz { 6.4483f };
    std::atomic<float> sawLevel { 0.70f };
    std::atomic<float> outputLevel { 0.25f };

    static_assert (std::atomic<float>::is_always_lock_free,
                   "Parameter stores must not take a lock on the message thread "
                   "or block the audio thread reading them.");
};
