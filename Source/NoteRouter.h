#pragma once

#include <cstdint>

#include "DSP/NoteEvent.h"
#include "DSP/NoteStack.h"

class SynthVoice;

//==============================================================================
/*
    Connects note input sources to the voice: owns the event FIFOs, owns the
    held-note stack, and translates resolved priority into the voice's
    noteOn/retargetPitch/noteOff calls.

    A peer class, not part of SynthVoice - dsp-voice-design.md section 6 keeps
    the voice free of MIDI, timers and patterns, and says items 5 and 7 become
    exactly this kind of class. This one is block-granular rather than
    sample-accurate, which is the deliberate scoping line for item 4; item 5's
    arp clock is where sample-accuracy actually matters.

    TWO FIFOs, not one. Each is strictly single-producer, and there are two
    distinct producer threads: the message thread (QWERTY + on-screen widget)
    and the MIDI thread. Sharing one queue between them would be undefined
    behaviour.

    See documents/note-handling-design.md sections 2, 3 and 7.
*/
class NoteRouter
{
public:
    // Explicit, because JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR below
    // declares a deleted copy constructor, and declaring any constructor
    // suppresses the implicit default one. SynthVoice does the same.
    NoteRouter() = default;

    //==============================================================================
    // MESSAGE THREAD only.
    void pushUiEvent (const NoteEvent& event) noexcept { uiEvents.push (event); }

    // MIDI THREAD only.
    void pushMidiEvent (const NoteEvent& event) noexcept { midiEvents.push (event); }

    //==============================================================================
    // AUDIO THREAD only - call at the top of a block, before rendering.
    void dispatchPendingEvents (SynthVoice& voice, NotePriorityMode priorityMode) noexcept;

    // AUDIO THREAD only. Clears held notes, so a device change can't leave a
    // phantom note held forever.
    void reset() noexcept;

private:
    void apply (const NoteEvent& event, SynthVoice& voice, NotePriorityMode priorityMode) noexcept;

    NoteEventFifo midiEvents;
    NoteEventFifo uiEvents;
    NoteStack noteStack;

    // Which note the voice is actually sounding right now, as opposed to which
    // notes are held. Audio-thread-private.
    //
    // This is what lets a key press that changes nothing audible avoid
    // retriggering the envelope - see the comment in apply().
    bool voiceIsSounding = false;
    std::uint8_t soundingNoteNumber = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteRouter)
};
