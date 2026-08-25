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
    /*
        Whether draining events should actually drive the voice.

        In TrackOnly the held-note stack is still updated exactly as before -
        held-note tracking stays live in BOTH modes, which is precisely what
        makes the arp on/off transitions cheap - but no voice.* call is made
        and voiceIsSounding is left alone. Those fields are then only ever
        written by whichever side actually owns the voice.

        See documents/arpeggiator-design.md section 6.
    */
    enum class VoiceDrive : int { Direct = 0, TrackOnly = 1 };

    //==============================================================================
    // AUDIO THREAD only - call at the top of a block, before rendering.
    void dispatchPendingEvents (SynthVoice& voice, NotePriorityMode priorityMode,
                                 VoiceDrive drive) noexcept;

    // AUDIO THREAD only. The arpeggiator walks this, read-only - it is never
    // moved out of here, because giving audio-thread-private state two owners
    // would buy nothing.
    const NoteStack& getNoteStack() const noexcept { return noteStack; }

    /*
        Stop driving the voice, leaving it SILENT.

        IDEMPOTENT, and paired with Arpeggiator::releaseVoice: MainComponent
        calls both on every arp on/off transition and exactly one of them
        actually does anything, which is what makes a stuck note unreachable by
        any toggle order. Without this, handing the voice to the arp would
        leave the router's sustained note with no note-off and its belief
        stale - so a later arp-OFF would give silence with a key still held
        (T1), which is as bad as a note stuck on.

        AUDIO THREAD.
    */
    void releaseVoice (SynthVoice& voice) noexcept;

    /*
        Take the voice back and sound the current held resolution AT ONCE,
        rather than waiting for the next key event (T2). The keys are still
        down, so something must sound again immediately - a chord held through
        an arp-OFF that stayed silent until the next press would be the same
        class of failure as a stuck note.

        Starts from silence, per the hand-over invariant, so this is a
        noteOn - never retargetPitch.

        AUDIO THREAD.
    */
    void retakeVoice (SynthVoice& voice, NotePriorityMode priorityMode) noexcept;

    // AUDIO THREAD only. Clears held notes, so a device change can't leave a
    // phantom note held forever.
    void reset() noexcept;

private:
    void apply (const NoteEvent& event, SynthVoice& voice, NotePriorityMode priorityMode,
                 VoiceDrive drive) noexcept;

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
