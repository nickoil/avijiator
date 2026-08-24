#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "DSP/NoteEvent.h"

//==============================================================================
/*
    Plays notes from the computer keyboard, tracker-style: two rows of keys
    spanning two octaves, with a shift for moving up and down.

        Z S X D C V G B H N J M   =  C C# D D# E F F# G G# A A# B
        Q 2 W 3 E R 5 T 6 Y 7 U   =  the same, one octave higher

        , = octave down        . = octave up

    Deliberately NOT Z/X for the octave shift, even though that pairing is
    common elsewhere - both are note keys in this layout.

    KEY-REPEAT IMMUNE BY CONSTRUCTION. Whether JUCE's key callbacks re-fire on
    the OS's auto-repeat isn't something we've verified, so this doesn't
    depend on it either way: it keeps its own record of which keys are held
    and DIFFS against a fresh poll. A repeat event finds the key already
    recorded as held, produces no transition, and emits nothing.

    See documents/note-handling-design.md section 7.
*/
class QwertyNoteInput
{
public:
    QwertyNoteInput() = default;

    // Emits into NoteRouter's UI FIFO. Called on the message thread only.
    std::function<void (const NoteEvent&)> onNoteEvent;

    // Re-poll every mapped key and emit an event for each change. Safe to
    // call as often as wanted - with nothing changed it does nothing.
    void pollKeyStates();

    // Releases everything currently held. Needed when the window loses
    // focus: no further key callbacks arrive, so without this a note held at
    // that moment would stick on forever.
    void releaseAllHeldKeys();

    int getOctaveShift() const noexcept { return octaveShift; }

private:
    struct KeyMapping
    {
        int keyCode;
        int semitoneOffset;
    };

    struct KeyState
    {
        bool isHeld = false;

        // The note number actually EMITTED when this key went down. The
        // note-off must repeat it exactly, or NoteStack won't find the entry
        // to remove and the note sticks on. Recomputing it at release time
        // would break the moment the octave shifts while a key is held.
        std::uint8_t emittedNoteNumber = 0;
    };

    static constexpr int numMappedKeys = 24;
    static const KeyMapping keyMap[numMappedKeys];

    // Z is C3. Shift is clamped so the highest mapped key stays inside MIDI's
    // 0..127 range.
    static constexpr int baseNoteNumber = 48;
    static constexpr int minOctaveShift = -2;
    static constexpr int maxOctaveShift = 4;

    static constexpr int octaveDownKeyCode = ',';
    static constexpr int octaveUpKeyCode = '.';

    void emit (NoteEvent::Type type, std::uint8_t noteNumber, float velocity);

    std::array<KeyState, numMappedKeys> keyStates {};
    bool octaveDownWasHeld = false;
    bool octaveUpWasHeld = false;
    int octaveShift = 0;
};
