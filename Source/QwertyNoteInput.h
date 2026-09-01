#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "DSP/NoteEvent.h"

struct VoiceParameters;

//==============================================================================
/*
    Plays notes from the computer keyboard, tracker-style: two rows of keys
    spanning two octaves, with a shift for moving up and down.

        Z S X D C V G B H N J M   =  C C# D D# E F F# G G# A A# B
        Q 2 W 3 E R 5 T 6 Y 7 U   =  the same, one octave higher

        , = octave down        . = octave up

    The octave shift itself lives in VoiceParameters::masterOctaveShift, not
    here - it's a global "note output" transpose shared with the on-screen
    keyboard, MIDI input, the arp and the sequencer (documents/
    note-handling-design.md section 7's revision), not a QWERTY-only quirk.
    This class only detects the comma/period keys and forwards to that shared
    atomic via adjustMasterOctaveShift; it emits raw, untransposed note
    numbers - the transpose is applied once, downstream, at the point each
    note source's final pitch reaches the voice.

    Deliberately NOT Z/X for the octave shift, even though that pairing is
    common elsewhere - both are note keys in this layout.

    WINDOWS VK QUIRK: isKeyCurrentlyDown() takes a raw virtual-key code, not
    an ASCII character. Letters/digits get away with passing the character
    anyway because VK_A..VK_Z and VK_0..VK_9 happen to equal 'A'..'Z' and
    '0'..'9' - but ',' (0x2C) and '.' (0x2E) collide with VK_SNAPSHOT (Print
    Screen) and VK_DELETE respectively, so passing the raw characters silently
    polls the wrong keys. See octaveDownKeyCode/octaveUpKeyCode below, and
    https://forum.juce.com/t/weird-keypress-keycode-quirks-in-windows-and-linux/59298.

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
    explicit QwertyNoteInput (VoiceParameters& parameters) noexcept : parameters (parameters) {}

    // Emits into NoteRouter's UI FIFO. Called on the message thread only.
    std::function<void (const NoteEvent&)> onNoteEvent;

    // Re-poll every mapped key and emit an event for each change. Safe to
    // call as often as wanted - with nothing changed it does nothing.
    void pollKeyStates();

    // Releases everything currently held. Needed when the window loses
    // focus: no further key callbacks arrive, so without this a note held at
    // that moment would stick on forever.
    void releaseAllHeldKeys();

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
    // 0..127 range - see VoiceParameters::minMasterOctaveShift/
    // maxMasterOctaveShift.
    static constexpr int baseNoteNumber = 48;

   #if JUCE_WINDOWS
    // VK_OEM_COMMA / VK_OEM_PERIOD - see the WINDOWS VK QUIRK note above.
    static constexpr int octaveDownKeyCode = 0xBC;
    static constexpr int octaveUpKeyCode = 0xBE;
   #else
    static constexpr int octaveDownKeyCode = ',';
    static constexpr int octaveUpKeyCode = '.';
   #endif

    void emit (NoteEvent::Type type, std::uint8_t noteNumber, float velocity);

    VoiceParameters& parameters;
    std::array<KeyState, numMappedKeys> keyStates {};
    bool octaveDownWasHeld = false;
    bool octaveUpWasHeld = false;
};
