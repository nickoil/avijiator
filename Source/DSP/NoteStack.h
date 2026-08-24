#pragma once

#include <array>
#include <cstdint>

//==============================================================================
/*
    Which held note wins when several are down at once.

    architecture.md leaves this explicitly open - "try both, keep whichever
    feels right by ear" - so it is a runtime switch rather than a hardcoded
    choice, and the comparison costs a combo-box change instead of a rebuild.

    Stored as a plain std::atomic<int> in VoiceParameters, no smoother: a
    discrete switch, matching envelopeDestination and lfoWaveform.
*/
enum class NotePriorityMode : int { LastNote = 0, HighestNote = 1 };

//==============================================================================
/*
    The set of currently-held notes, plus the rule for picking which one the
    mono voice should actually sound.

    AUDIO-THREAD-PRIVATE. This is driven by NoteRouter after draining the
    event FIFOs, so it is touched by exactly one thread ever - the same
    category as SynthVoice's own gated-state flag. Two reasons it lives here
    rather than at the point events are produced:

      1. There are two producer threads (message and MIDI). A stack shared
         between them would reintroduce exactly the race the FIFO exists to
         eliminate.
      2. Live priority-mode switching needs the full held-note set to still
         exist. If priority were resolved upstream and only "the note that
         should sound" were queued, switching modes mid-performance would have
         nothing left to re-resolve from.

    See documents/note-handling-design.md section 3.
*/
class NoteStack
{
public:
    struct Resolution
    {
        bool isSounding = false;        // false => nothing is held any more
        std::uint8_t noteNumber = 0;
        float pitchLog2Hz = 0.0f;
        float velocity = 0.0f;
    };

    void reset() noexcept { numHeld = 0; }

    bool isEmpty() const noexcept { return numHeld == 0; }
    int getNumHeldNotes() const noexcept { return numHeld; }

    Resolution noteOn (std::uint8_t noteNumber, float pitchLog2Hz, float velocity,
                        NotePriorityMode mode) noexcept;

    Resolution noteOff (std::uint8_t noteNumber, NotePriorityMode mode) noexcept;

private:
    struct HeldNote
    {
        std::uint8_t noteNumber = 0;
        float pitchLog2Hz = 0.0f;
        float velocity = 0.0f;
    };

    // 16, not 128: nobody holds more than ten fingers' worth, and this array
    // is scanned only on note events, never per-sample, so the headroom is
    // free. A 17th simultaneous note is dropped rather than growing the
    // array - no allocation, ever, on any thread.
    static constexpr int maxHeldNotes = 16;

    void removeIfPresent (std::uint8_t noteNumber) noexcept;
    Resolution resolve (NotePriorityMode mode) const noexcept;

    std::array<HeldNote, (size_t) maxHeldNotes> held {};
    int numHeld = 0;
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup. Priority resolution failing is
    silent - a wrong fallback note sounds like a playing mistake, not a bug -
    so the design doc's test recipes are asserted here rather than left
    entirely to the ear.
*/
void runNoteStackSelfTest();

#endif
