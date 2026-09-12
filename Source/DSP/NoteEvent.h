/*
    This file is part of Avijiator.
    Copyright (C) 2026 Nick Casey

    Avijiator is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version.

    Avijiator is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with Avijiator. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <juce_core/juce_core.h>

//==============================================================================
/*
    One note event, as produced by any input source (MIDI, QWERTY, on-screen
    widget) and consumed by the audio thread.

    Deliberately a trivially-copyable POD - it is copied in and out of a
    lock-free ring buffer, so it must own no heap memory and have no
    non-trivial constructor or destructor.

    See documents/note-handling-design.md section 2.
*/
struct NoteEvent
{
    enum class Type : std::uint8_t { NoteOn, NoteOff };

    Type type = Type::NoteOff;

    // The note's identity, 0..127. Shared across every input source, so a
    // NoteOff can be matched back to the NoteOn that started it regardless of
    // which source produced either one.
    std::uint8_t noteNumber = 0;

    // log2(Hz), matching the pitch domain used everywhere else in the voice.
    // Converted at the PRODUCER (message or MIDI thread), so the audio thread
    // never computes a logarithm. Only meaningful for NoteOn.
    float pitchLog2Hz = 0.0f;

    // 0..1. Captured but not routed anywhere yet - item 7's accent and
    // character-and-vim.md B5's velocity routing are the eventual consumers.
    // Only meaningful for NoteOn.
    float velocity = 0.0f;
};

//==============================================================================
/*
    MIDI note number -> log2(Hz). Called at the PRODUCER (message or MIDI
    thread) so the audio thread never computes a logarithm, and so every input
    source converts identically.

    log2(440) = 8.78136, and MIDI note 69 is A4 = 440Hz by definition.

    constexpr, not just inline: VoiceParameters' step-sequencer default
    (housekeeping, documents/TODO.md) computes a default step pitch from a
    named MIDI note at compile time rather than hand-carrying the resulting
    float literal.
*/
constexpr float pitchLog2HzForMidiNote (int midiNoteNumber) noexcept
{
    return 8.78136f + (float) (midiNoteNumber - 69) / 12.0f;
}

//==============================================================================
/*
    Single-producer / single-consumer lock-free queue carrying NoteEvents from
    an input thread to the audio thread.

    STRICTLY ONE PRODUCER PER INSTANCE. There are two distinct non-audio
    producer threads in this app - the message thread (QWERTY + on-screen
    widget) and the MIDI thread - so NoteRouter owns TWO instances of this
    class rather than sharing one. Calling push() on the same instance from
    two threads is undefined behaviour, not merely slow.

    Hand-rolled rather than juce::AbstractFifo, matching the project's
    precedent (oscillator, filter, ADSR, LFO) of owning this kind of
    infrastructure rather than depending on library semantics we haven't
    verified.

    See documents/note-handling-design.md section 2 for the full rationale,
    including why the memory ordering below is what it is.
*/
class NoteEventFifo
{
public:
    // 64 is far more headroom than a live-played mono synth can use: the
    // queue is drained to empty every block, so sustaining more than this per
    // block period would mean thousands of events per second - a malfunctioning
    // source, not playing.
    static constexpr int capacity = 64;

    //==============================================================================
    // PRODUCER THREAD ONLY.
    bool push (const NoteEvent& event) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto r = readIndex.load (std::memory_order_acquire);

        if (w - r >= (unsigned) capacity)
        {
            // Overflow drops the NEWEST event, keeping what is already queued:
            // a lost NoteOff is much worse than a lost NoteOn, because it
            // means a stuck note whose envelope never releases.
            //
            // Safe to assert here - this is the producer thread (message or
            // MIDI), never the audio thread, so CLAUDE.md's audio-thread ban
            // on this kind of thing does not apply.
            jassertfalse;
            return false;
        }

        buffer[w & indexMask] = event;

        // Release: pairs with the consumer's acquire-load of writeIndex, so
        // the slot write above cannot be reordered after this store and be
        // read as garbage.
        writeIndex.store (w + 1, std::memory_order_release);
        return true;
    }

    //==============================================================================
    // AUDIO THREAD ONLY.
    bool pop (NoteEvent& event) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);

        if (r == w)
            return false;

        event = buffer[r & indexMask];

        // Release: pairs with the producer's acquire-load of readIndex, so the
        // slot read above cannot be reordered after this store and race with
        // the producer overwriting that slot on wraparound.
        readIndex.store (r + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr unsigned indexMask = (unsigned) capacity - 1u;

    std::array<NoteEvent, (size_t) capacity> buffer {};

    // Monotonically increasing and never wrapped themselves - only the array
    // subscript (index & indexMask) wraps. Unsigned overflow is well-defined
    // modular arithmetic, so `w - r` stays correct even across the wrap. This
    // avoids the classic bug where a wrapped head==tail is ambiguous between
    // "empty" and "completely full".
    //
    // Each index is written by exactly one thread, so that thread's load of
    // its OWN index is relaxed - nothing else can be writing it.
    std::atomic<unsigned> writeIndex { 0 };
    std::atomic<unsigned> readIndex { 0 };

    static_assert ((capacity & (capacity - 1)) == 0,
                   "capacity must be a power of two for the index mask to work");
    static_assert (std::atomic<unsigned>::is_always_lock_free,
                   "index updates must not take a lock - the consumer is the audio thread");
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup. The FIFO's correctness is
    single-threaded logic (ordering, masking, full/empty boundaries) plus
    memory ordering; the former is exactly what this covers, and getting it
    wrong would be SILENT - a dropped or duplicated note event rather than a
    crash.

    Not a substitute for reasoning about the memory ordering, which no
    single-threaded test can exercise. See the design doc's section 2.
*/
inline void runNoteEventFifoSelfTest()
{
    NoteEventFifo fifo;
    NoteEvent out;

    // A fresh queue is empty.
    jassert (! fifo.pop (out));

    // Round-trip preserves every field.
    const NoteEvent in { NoteEvent::Type::NoteOn, 60, 8.0f, 0.75f };
    jassert (fifo.push (in));
    jassert (fifo.pop (out));
    jassert (out.type == in.type);
    jassert (out.noteNumber == in.noteNumber);
    jassert (out.pitchLog2Hz == in.pitchLog2Hz);
    jassert (out.velocity == in.velocity);
    jassert (! fifo.pop (out));

    // Order is preserved - it is a queue, not a stack.
    for (int i = 0; i < 3; ++i)
        jassert (fifo.push ({ NoteEvent::Type::NoteOn, (std::uint8_t) i, 0.0f, 0.0f }));

    for (int i = 0; i < 3; ++i)
    {
        jassert (fifo.pop (out));
        jassert (out.noteNumber == (std::uint8_t) i);
    }

    // Exactly-full is allowed, and nothing is lost at that boundary.
    for (int i = 0; i < NoteEventFifo::capacity; ++i)
        jassert (fifo.push ({ NoteEvent::Type::NoteOn, (std::uint8_t) i, 0.0f, 0.0f }));

    for (int i = 0; i < NoteEventFifo::capacity; ++i)
    {
        jassert (fifo.pop (out));
        jassert (out.noteNumber == (std::uint8_t) i);
    }
    jassert (! fifo.pop (out));

    // Wraparound. Indices are monotonic and only (index & mask) wraps, so
    // pushing well past capacity in batches must keep working - this is where
    // an off-by-one in the mask or a wrapped-index comparison would show up.
    for (int batch = 0; batch < 5; ++batch)
    {
        for (int i = 0; i < 40; ++i)
            jassert (fifo.push ({ NoteEvent::Type::NoteOn, (std::uint8_t) i, 0.0f, 0.0f }));

        for (int i = 0; i < 40; ++i)
        {
            jassert (fifo.pop (out));
            jassert (out.noteNumber == (std::uint8_t) i);
        }
    }

    jassert (! fifo.pop (out));

    // Deliberate overflow is NOT exercised here: push() asserts on overflow by
    // design, so testing it would trip that assert and halt. The boundary that
    // actually matters - exactly-full, nothing lost - is covered above.
}

#endif
