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

#include "NoteStack.h"

#include <juce_core/juce_core.h>

NoteStack::Resolution NoteStack::noteOn (std::uint8_t noteNumber, float pitchLog2Hz,
                                          float velocity, NotePriorityMode mode) noexcept
{
    // Refresh rather than duplicate. A note pressed again without an
    // intervening release - a stuck MIDI note, or a source that doesn't
    // guarantee clean on/off pairing - moves back to "most recently pressed"
    // instead of leaving two entries that would each need releasing.
    removeIfPresent (noteNumber);

    if (numHeld < maxHeldNotes)
        held[(size_t) numHeld++] = { noteNumber, pitchLog2Hz, velocity };

    return resolve (mode);
}

NoteStack::Resolution NoteStack::noteOff (std::uint8_t noteNumber, NotePriorityMode mode) noexcept
{
    // A note-off for something not held is a no-op, mirroring how
    // Adsr::noteOff() while Idle does nothing rather than misbehaving.
    removeIfPresent (noteNumber);
    return resolve (mode);
}

void NoteStack::removeIfPresent (std::uint8_t noteNumber) noexcept
{
    for (int i = 0; i < numHeld; ++i)
    {
        if (held[(size_t) i].noteNumber == noteNumber)
        {
            // Compact by shifting down, which PRESERVES PRESS ORDER - that
            // ordering is exactly what LastNote priority reads. A cheaper
            // swap-with-last would destroy it.
            for (int j = i; j < numHeld - 1; ++j)
                held[(size_t) j] = held[(size_t) j + 1];

            --numHeld;
            return;
        }
    }
}

NoteStack::Resolution NoteStack::resolve (NotePriorityMode mode) const noexcept
{
    if (numHeld == 0)
        return {}; // isSounding stays false - nothing held, voice should release

    auto winningIndex = numHeld - 1; // LastNote: most recently pressed, still held

    if (mode == NotePriorityMode::HighestNote)
    {
        winningIndex = 0;

        for (int i = 1; i < numHeld; ++i)
            if (held[(size_t) i].noteNumber > held[(size_t) winningIndex].noteNumber)
                winningIndex = i;
    }

    const auto& winner = held[(size_t) winningIndex];
    return { true, winner.noteNumber, winner.pitchLog2Hz, winner.velocity };
}

//==============================================================================
#if JUCE_DEBUG

namespace
{
    // Any deterministic, distinguishable value - this is checking that the
    // pitch travels through the stack intact, not that the conversion itself
    // is right (that lives at the producer, steps 5 and 7).
    float testPitchFor (int noteNumber) noexcept { return (float) noteNumber * 0.01f; }
}

void runNoteStackSelfTest()
{
    // MIDI note numbers used throughout: C4 = 60, E4 = 64, G4 = 67.
    constexpr std::uint8_t c4 = 60, e4 = 64, g4 = 67;

    const auto press = [] (NoteStack& s, std::uint8_t n, NotePriorityMode m)
    {
        return s.noteOn (n, testPitchFor (n), 1.0f, m);
    };

    //==========================================================================
    // Recipe: "priority, differentiating" - hold E, then press a LOWER note.
    // This separates the two modes without releasing anything.
    {
        NoteStack stack;
        auto r = press (stack, e4, NotePriorityMode::LastNote);
        jassert (r.isSounding && r.noteNumber == e4);

        r = press (stack, c4, NotePriorityMode::LastNote);
        jassert (r.isSounding && r.noteNumber == c4); // most recent wins
        jassert (r.pitchLog2Hz == testPitchFor (c4)); // pitch travelled intact
    }
    {
        NoteStack stack;
        auto r = press (stack, e4, NotePriorityMode::HighestNote);
        jassert (r.isSounding && r.noteNumber == e4);

        r = press (stack, c4, NotePriorityMode::HighestNote);
        jassert (r.isSounding && r.noteNumber == e4); // lower note is ignored
    }

    //==========================================================================
    // Recipe: "must not go silent" - releasing the sounding note while others
    // are still held must fall back, not stop. Pressed in the order C4, G4,
    // E4, so recency order and pitch order deliberately disagree.
    {
        NoteStack stack;
        constexpr auto mode = NotePriorityMode::LastNote;

        press (stack, c4, mode);
        press (stack, g4, mode);
        auto r = press (stack, e4, mode);
        jassert (r.isSounding && r.noteNumber == e4); // most recent of the three

        r = stack.noteOff (e4, mode);
        jassert (r.isSounding && r.noteNumber == g4); // falls back by RECENCY, not pitch

        r = stack.noteOff (g4, mode);
        jassert (r.isSounding && r.noteNumber == c4);

        r = stack.noteOff (c4, mode);
        jassert (! r.isSounding); // only now does the voice release
        jassert (stack.isEmpty());
    }
    {
        NoteStack stack;
        constexpr auto mode = NotePriorityMode::HighestNote;

        press (stack, c4, mode);
        press (stack, g4, mode);
        auto r = press (stack, e4, mode);
        jassert (r.isSounding && r.noteNumber == g4); // highest, NOT most recent

        r = stack.noteOff (e4, mode);
        jassert (r.isSounding && r.noteNumber == g4); // unchanged - E4 was never sounding

        r = stack.noteOff (g4, mode);
        jassert (r.isSounding && r.noteNumber == c4);

        r = stack.noteOff (c4, mode);
        jassert (! r.isSounding);
    }

    //==========================================================================
    // A re-press without a release refreshes recency instead of duplicating.
    // If it duplicated, one note-off would leave a phantom entry behind and
    // the voice would never release - a stuck note.
    {
        NoteStack stack;
        constexpr auto mode = NotePriorityMode::LastNote;

        press (stack, c4, mode);
        press (stack, e4, mode);
        auto r = press (stack, c4, mode); // pressed again, never released
        jassert (r.isSounding && r.noteNumber == c4);
        jassert (stack.getNumHeldNotes() == 2); // refreshed, not duplicated

        r = stack.noteOff (c4, mode);
        jassert (r.isSounding && r.noteNumber == e4);

        r = stack.noteOff (e4, mode);
        jassert (! r.isSounding);
    }

    //==========================================================================
    // A note-off for something never held changes nothing.
    {
        NoteStack stack;
        constexpr auto mode = NotePriorityMode::LastNote;

        press (stack, c4, mode);
        const auto r = stack.noteOff (99, mode);
        jassert (r.isSounding && r.noteNumber == c4);
        jassert (stack.getNumHeldNotes() == 1);
    }

    //==========================================================================
    // Capacity: the 17th simultaneous note is dropped, and dropping it must
    // not corrupt the 16 already held.
    {
        NoteStack stack;
        constexpr auto mode = NotePriorityMode::LastNote;

        for (int i = 0; i < 16; ++i)
            press (stack, (std::uint8_t) (40 + i), mode);

        jassert (stack.getNumHeldNotes() == 16);

        const auto r = press (stack, 99, mode);
        jassert (stack.getNumHeldNotes() == 16);       // refused to grow
        jassert (r.isSounding && r.noteNumber == 55);  // last of the 16 still wins
    }

    //==========================================================================
    // THE PRESS-ORDER GUARANTEE THE ARPEGGIATOR DEPENDS ON. Item 5's AsPlayed
    // pattern walks getHeldNotes() directly, so "compacting a removal
    // preserves press order" stops being an internal implementation detail
    // and becomes a contract - asserted here rather than left as a comment on
    // removeIfPresent.
    {
        NoteStack stack;
        constexpr auto mode = NotePriorityMode::LastNote;

        press (stack, c4, mode);
        press (stack, g4, mode);
        press (stack, e4, mode);

        auto held = stack.getHeldNotes();
        jassert (held.size() == 3);
        jassert (held[0].noteNumber == c4 && held[1].noteNumber == g4 && held[2].noteNumber == e4);

        // Removing from the MIDDLE is the case that would break if the
        // compaction swapped with the last entry instead of shifting down.
        stack.noteOff (g4, mode);

        held = stack.getHeldNotes();
        jassert (held.size() == 2);
        jassert (held[0].noteNumber == c4 && held[1].noteNumber == e4);
        jassert (held[0].pitchLog2Hz == testPitchFor (c4)); // the whole note travels, not just its number

        // getCurrentResolution re-resolves the CURRENT set without a note
        // event happening - what the arpeggiator calls when it hands the voice
        // back with the keys still down.
        const auto r = stack.getCurrentResolution (mode);
        jassert (r.isSounding && r.noteNumber == e4);
        jassert (stack.getCurrentResolution (NotePriorityMode::HighestNote).noteNumber == e4);
        jassert (stack.getNumHeldNotes() == 2); // and changes nothing
    }
    {
        // An empty stack yields an empty view and a silent resolution, so the
        // arp's "nothing held" path is reached rather than a stale note.
        NoteStack stack;
        jassert (stack.getHeldNotes().empty());
        jassert (! stack.getCurrentResolution (NotePriorityMode::LastNote).isSounding);
    }

    //==========================================================================
    // reset() clears everything, for SynthVoice::reset()'s benefit.
    {
        NoteStack stack;
        press (stack, c4, NotePriorityMode::LastNote);
        stack.reset();
        jassert (stack.isEmpty());
    }
}

#endif
