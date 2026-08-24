#include "NoteRouter.h"

#include "DSP/SynthVoice.h"

void NoteRouter::reset() noexcept
{
    noteStack.reset();
    voiceIsSounding = false;
    soundingNoteNumber = 0;
}

void NoteRouter::dispatchPendingEvents (SynthVoice& voice, NotePriorityMode priorityMode) noexcept
{
    NoteEvent event;

    // Each queue is drained fully in turn. Ordering WITHIN a source is always
    // preserved; ordering BETWEEN the two sources within a single block is
    // not. At block granularity that's inaudible, but it is a real limitation
    // rather than an oversight.
    while (midiEvents.pop (event))
        apply (event, voice, priorityMode);

    while (uiEvents.pop (event))
        apply (event, voice, priorityMode);
}

void NoteRouter::apply (const NoteEvent& event, SynthVoice& voice, NotePriorityMode priorityMode) noexcept
{
    if (event.type == NoteEvent::Type::NoteOn)
    {
        const auto resolution = noteStack.noteOn (event.noteNumber, event.pitchLog2Hz,
                                                   event.velocity, priorityMode);

        // A note-on that doesn't change which note is sounding must NOT reach
        // the voice, or Retrigger mode would re-pluck for no audible reason.
        //
        // This happens for real in HighestNote mode: hold G, press a lower C,
        // and G keeps sounding - the C is swallowed by the priority rule. A
        // key press that changes nothing you can hear should not produce an
        // envelope thump. (In LastNote mode a new press always wins, so this
        // only ever bites in HighestNote.)
        if (! voiceIsSounding || resolution.noteNumber != soundingNoteNumber)
            voice.noteOn (resolution.pitchLog2Hz, resolution.velocity);

        voiceIsSounding = true;
        soundingNoteNumber = resolution.noteNumber;
    }
    else
    {
        const auto resolution = noteStack.noteOff (event.noteNumber, priorityMode);

        if (resolution.isSounding)
        {
            // Something is still held, so the voice keeps playing and only
            // changes pitch. retargetPitch rather than noteOn is the whole
            // reason that method exists: nothing was newly PRESSED here, so
            // this must never retrigger, in either legato mode.
            if (resolution.noteNumber != soundingNoteNumber)
                voice.retargetPitch (resolution.pitchLog2Hz);

            soundingNoteNumber = resolution.noteNumber;
        }
        else
        {
            voice.noteOff();
            voiceIsSounding = false;
        }
    }
}
