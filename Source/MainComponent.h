#pragma once

#include <array>
#include <functional>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "Arpeggiator.h"
#include "DSP/NoteEvent.h"
#include "DSP/NoteStack.h"
#include "DSP/StepClock.h"
#include "DSP/SynthVoice.h"
#include "NoteRouter.h"
#include "QwertyNoteInput.h"

//==============================================================================
/*
    Top-level content component.

    Item 2 (oscillator + filter core) complete: four sources -> 24dB resonant
    lowpass -> VCA, nine debug controls. This scaffolding is explicitly
    throwaway - item 6 is the real UI pass and none of it survives that.
    See documents/dsp-voice-design.md for how it was built.

    Item 3 (envelope + LFO): shared ADSR (routable VCF/VCA/Both) plus a
    triangle/square/S&H LFO (routable pitch and/or cutoff, independently).

    Item 4 (note handling): playable from MIDI hardware, the computer
    keyboard, and an on-screen keyboard - all three through NoteRouter's
    FIFOs and the held-note stack, so priority, glide and legato behave
    identically whichever one is used. The two latching note buttons remain,
    since a mouse can't hold two momentary keys at once.

    All of this scaffolding is explicitly throwaway; item 6 is the real UI
    pass. See documents/note-handling-design.md.
*/
class MainComponent final : public juce::AudioAppComponent,
                             private juce::MidiInputCallback
{
public:
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    //==============================================================================
    // Computer-keyboard note input. keyStateChanged fires on both press and
    // release (unlike keyPressed), which is what a note-off needs - and
    // QwertyNoteInput diffs a fresh poll rather than trusting the event
    // itself, so OS auto-repeat can't produce duplicate note-ons.
    bool keyStateChanged (bool isKeyDown) override;

    // Without this, a note held as the window loses focus would stick on
    // forever - no further key callbacks arrive to release it.
    void focusLost (FocusChangeType cause) override;

private:
    //==============================================================================
    // Fires on the MIDI THREAD - not the message thread, and not the audio
    // thread. That's precisely why NoteRouter keeps a second FIFO: each queue
    // is strictly single-producer, and this is a different producer from the
    // QWERTY/on-screen path.
    void handleIncomingMidiMessage (juce::MidiInput* source,
                                     const juce::MidiMessage& message) override;

    void enableAllMidiInputs();

private:
    //==============================================================================
    // Throwaway auditioning scaffolding. Item 6 is the real UI pass and none of
    // this survives it - deliberately unstyled, and driven by one spec table
    // plus one loop so later steps add a row rather than more copy-paste.
    struct DebugControl
    {
        juce::Slider slider;
        juce::Label label;
    };

    struct DebugControlSpec
    {
        const char* name;
        double minimum, maximum, defaultValue;
        bool storeAsLog2;                             // true for Hz-valued controls
        std::atomic<float> VoiceParameters::* target;
    };

    // Sizing the definition to this count makes the compiler enforce that the
    // table and the array stay in step.
    static constexpr int numDebugControls = 19;
    static const DebugControlSpec debugControlSpecs[numDebugControls];

    // Discrete switches (Envelope Destination, later LFO Waveform) don't fit
    // the float-slider table above - a small parallel table rather than
    // complicating that one with a variant type.
    struct DebugChoice
    {
        juce::ComboBox comboBox;
        juce::Label label;
    };

    struct DebugChoiceSpec
    {
        const char* name;
        const char* const* choices;
        int numChoices;
        int defaultIndex;
        std::atomic<int> VoiceParameters::* target;
    };

    static constexpr int numDebugChoiceControls = 8;
    static const DebugChoiceSpec debugChoiceSpecs[numDebugChoiceControls];

    // A button that reports press AND release, not just "clicked".
    //
    // juce::Button::onStateChange would be the shorter route, but its exact
    // firing behaviour isn't something to assume without reading JUCE sources
    // - overriding mouseDown/mouseUp is unambiguous, since Component's
    // mouse-capture behaviour (mouseUp still reaches the component that
    // started the drag, even outside its bounds) is foundational and certain.
    //
    // Introduced for item 3's Gate button, then reused unchanged by the note
    // buttons and the on-screen keyboard - which is why it was worth
    // generalising rather than leaving Gate-specific.
    struct MomentaryButton final : public juce::TextButton
    {
        std::function<void (bool)> onPressedChanged;

        void mouseDown (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseDown (e);
            if (onPressedChanged != nullptr)
                onPressedChanged (true);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseUp (e);
            if (onPressedChanged != nullptr)
                onPressedChanged (false);
        }
    };

    // Two note triggers a fifth apart, for exercising glide and
    // legato/retrigger with the mouse alone.
    //
    // They LATCH rather than being momentary - a mouse has one pointer, so
    // momentary buttons could never be held together, and overlapping notes
    // are the entire point of those tests. Kept even now that QWERTY and MIDI
    // exist, since neither of those helps if you only have a mouse to hand.
    struct NoteButtonSpec { const char* name; int midiNoteNumber; };

    static constexpr int numNoteButtons = 2;
    static const NoteButtonSpec noteButtonSpecs[numNoteButtons];

    //==============================================================================
    // A clickable one-octave keyboard, C3 to C4.
    //
    // Hand-rolled from MomentaryButton rather than using
    // juce::MidiKeyboardComponent. That widget is real and capable, but it's a
    // large unverified surface (its own listener interface, click-position
    // velocity, scrolling) to bridge into the FIFO - for scaffolding item 6
    // throws away entirely. Reusing a pattern already proven by a successful
    // build beats introducing a new one here.
    //
    // MOMENTARY, unlike the two latching note buttons: click-and-hold is the
    // natural behaviour for a piano key, and a mouse can only press one at a
    // time regardless. Overlapping notes come from QWERTY or the latching
    // buttons.
    //
    // Fixed range, deliberately NOT following QWERTY's octave shift: a shift
    // while a key was held would make the note-off carry a different note
    // number than its note-on, and the note would stick on.
    struct KeyboardKeySpec { const char* name; int semitoneOffset; bool isBlackKey; };

    static constexpr int numKeyboardKeys = 13;
    static constexpr int keyboardBaseNoteNumber = 48; // C3, same as QWERTY's base
    static const KeyboardKeySpec keyboardKeySpecs[numKeyboardKeys];

    SynthVoice voice;

    // Owns the event FIFOs and the note-priority stack. Input sources push
    // into it from their own threads; the audio thread drains it at the top
    // of each block.
    NoteRouter router;
    QwertyNoteInput qwertyInput;

    // A peer of the voice and the router, not part of either: it owns the step
    // clock, the walker and the gate, and decides WHICH note happens WHEN.
    // When it is switched off the audio path below is byte-identical to item
    // 4's.
    Arpeggiator arp;

    // AUDIO-THREAD-PRIVATE. Edge-detects the arpEnabled atomic, because
    // switching the arp on or off is a HAND-OVER of the voice between two
    // owners, and both sides have to be told - see
    // documents/arpeggiator-design.md section 7.
    bool arpWasOn = false;

    // Opens JUCE's device selector. Without it the app just takes whatever
    // JUCE defaults to - on Windows that's WASAPI shared mode, whose latency
    // is high enough to get in the way of playing. ASIO is compiled in
    // (JUCE_ASIO), so an ASIO driver can be selected here. Also carries the
    // MIDI input device list.
    void showAudioSettings();
    juce::TextButton audioSettingsButton;

    std::array<DebugControl, numDebugControls> debugControls;
    std::array<DebugChoice, numDebugChoiceControls> debugChoiceControls;
    std::array<MomentaryButton, numNoteButtons> noteButtons;

    // The note buttons LATCH rather than being momentary - see the comment on
    // their wiring in the constructor. Message-thread state only.
    std::array<bool, numNoteButtons> noteButtonLatched {};
    std::array<MomentaryButton, numKeyboardKeys> keyboardButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
