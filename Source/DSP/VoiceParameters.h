#pragma once

#include <atomic>

#include "Lfo.h"
#include "NoteStack.h"

//==============================================================================
/*
    Whether an overlapping note-on restarts the envelope.

    Governs note-ON only. A note-off that reveals another still-held note
    underneath is NEVER a retrigger in either mode - nothing was newly
    pressed - which is why SynthVoice has a separate retargetPitch() method.
    See documents/note-handling-design.md section 5.
*/
enum class LegatoRetriggerMode : int { Retrigger = 0, Legato = 1 };

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
    // NOTE: there is no pitch parameter here any more. Pitch now arrives as
    // note events through NoteRouter's FIFOs and is owned by SynthVoice's
    // Glide, not set from the UI - see documents/note-handling-design.md
    // section 6.
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

    // NOTE: item 3's `gate` atomic is gone. Keeping it alongside real note
    // input would mean two independently edge-detected triggers of the same
    // envelope with no defined precedence - the exact race the note FIFO
    // exists to eliminate. Its removal is a correctness requirement, not
    // tidying - see documents/note-handling-design.md section 6.

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

    //==============================================================================
    // Item 4 (note handling).

    // Discrete, no smoother - matches envelopeDestination and lfoWaveform.
    // Retrigger by default: it continues item 3's already-ear-verified
    // behaviour, where every note-on plucks. Legato is the opt-in mode.
    std::atomic<int> legatoRetriggerMode { (int) LegatoRetriggerMode::Retrigger };

    // Discrete, no smoother. architecture.md leaves last-vs-highest note
    // priority explicitly open ("try both by ear"), so this is a runtime
    // switch and LastNote is only a provisional pick - the near-universal
    // mono-synth default, including the SH-101's.
    std::atomic<int> notePriorityMode { (int) NotePriorityMode::LastNote };

    // Seconds to glide ONE OCTAVE - a rate, not a per-interval duration. Time
    // constant, so NOT smoothed, same as the ADSR times and lfoRateHz.
    // 0 = instant/off, which is the safe first-load default.
    std::atomic<float> glideTimeSeconds { 0.0f };

    static_assert (std::atomic<float>::is_always_lock_free,
                   "Parameter stores must not take a lock on the message thread "
                   "or block the audio thread reading them.");
    static_assert (std::atomic<int>::is_always_lock_free,
                   "Same requirement as the float parameters above.");
};
