#pragma once

#include <atomic>

//==============================================================================
/*
    Every UI -> audio parameter for the voice lives here as an atomic, per
    CLAUDE.md: UI -> audio changes go through atomics, never a shared mutable
    field. SynthVoice snapshots these once per block and smooths them per
    sample - see documents/dsp-voice-design.md section 4.

    Grows incrementally as each build step lands its oscillator/filter tap
    (documents/dsp-voice-design.md section 7). Step 1 only needs outputLevel;
    pitch, source levels, pulse width, cutoff and resonance are added by the
    steps that introduce the thing they control.
*/
struct VoiceParameters
{
    std::atomic<float> outputLevel { 0.25f };

    static_assert (std::atomic<float>::is_always_lock_free,
                   "Parameter stores must not take a lock on the message thread "
                   "or block the audio thread reading them.");
};
