#include "QwertyNoteInput.h"

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Semitone offsets from the base note. Lower row starts at the base, upper
// row an octave above it.
const QwertyNoteInput::KeyMapping QwertyNoteInput::keyMap[numMappedKeys] =
{
    { 'Z',  0 }, { 'S',  1 }, { 'X',  2 }, { 'D',  3 }, { 'C',  4 }, { 'V',  5 },
    { 'G',  6 }, { 'B',  7 }, { 'H',  8 }, { 'N',  9 }, { 'J', 10 }, { 'M', 11 },

    { 'Q', 12 }, { '2', 13 }, { 'W', 14 }, { '3', 15 }, { 'E', 16 }, { 'R', 17 },
    { '5', 18 }, { 'T', 19 }, { '6', 20 }, { 'Y', 21 }, { '7', 22 }, { 'U', 23 },
};

void QwertyNoteInput::emit (NoteEvent::Type type, std::uint8_t noteNumber, float velocity)
{
    if (onNoteEvent != nullptr)
        onNoteEvent ({ type, noteNumber, pitchLog2HzForMidiNote (noteNumber), velocity });
}

void QwertyNoteInput::pollKeyStates()
{
    // Octave shift first, on the RISING edge only - it's an action, not a
    // held state. Doing it before the note scan means a key pressed in the
    // same poll lands in the new octave.
    //
    // Notes already held keep their original note number regardless, because
    // KeyState remembers what it emitted - see the header.
    const auto octaveDownNow = juce::KeyPress::isKeyCurrentlyDown (octaveDownKeyCode);
    const auto octaveUpNow = juce::KeyPress::isKeyCurrentlyDown (octaveUpKeyCode);

    if (octaveDownNow && ! octaveDownWasHeld)
        octaveShift = juce::jmax (minOctaveShift, octaveShift - 1);

    if (octaveUpNow && ! octaveUpWasHeld)
        octaveShift = juce::jmin (maxOctaveShift, octaveShift + 1);

    octaveDownWasHeld = octaveDownNow;
    octaveUpWasHeld = octaveUpNow;

    //==========================================================================
    for (int i = 0; i < numMappedKeys; ++i)
    {
        const auto& mapping = keyMap[i];
        auto& state = keyStates[(size_t) i];

        const auto isDownNow = juce::KeyPress::isKeyCurrentlyDown (mapping.keyCode);

        if (isDownNow == state.isHeld)
            continue; // no transition - this is what makes auto-repeat a no-op

        if (isDownNow)
        {
            const auto noteNumber = juce::jlimit (0, 127,
                                                   baseNoteNumber + octaveShift * 12 + mapping.semitoneOffset);

            state.emittedNoteNumber = (std::uint8_t) noteNumber;

            // A computer keyboard has no velocity sensing, so everything
            // plays at full. Item 4 doesn't route velocity anywhere yet in
            // any case.
            emit (NoteEvent::Type::NoteOn, state.emittedNoteNumber, 1.0f);
        }
        else
        {
            emit (NoteEvent::Type::NoteOff, state.emittedNoteNumber, 0.0f);
        }

        state.isHeld = isDownNow;
    }
}

void QwertyNoteInput::releaseAllHeldKeys()
{
    for (auto& state : keyStates)
    {
        if (! state.isHeld)
            continue;

        emit (NoteEvent::Type::NoteOff, state.emittedNoteNumber, 0.0f);
        state.isHeld = false;
    }

    // Not cleared: octaveShift, which is a setting rather than a held state
    // and should survive the window losing focus.
    octaveDownWasHeld = false;
    octaveUpWasHeld = false;
}
