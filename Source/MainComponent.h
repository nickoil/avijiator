#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "Arpeggiator.h"
#include "DSP/NoteEvent.h"
#include "DSP/NoteStack.h"
#include "DSP/StepClock.h"
#include "DSP/SynthVoice.h"
#include "NoteRouter.h"
#include "QwertyNoteInput.h"
#include "StepSequencer.h"
#include "UI/SynthPanel.h"

//==============================================================================
/*
    Top-level content component.

    Item 6 (UI pass, documents/ui-design.md) replaced every widget that used
    to live here directly with SynthPanel, the real instrument panel built at
    a fixed design size. This class now owns only what SynthPanel has no
    business owning: the audio callbacks, the MIDI callback, keyStateChanged /
    focusLost, showAudioSettings (needs the AudioDeviceManager), the six
    Debug self-tests, and the scale transform that maps SynthPanel's fixed
    canvas onto whatever the real window is (documents/ui-design.md section
    5's "The scaling, in MainComponent::resized()").

    Nothing in the signal path changed to get here - see that document's
    opening line, "this item adds zero code to getNextAudioBlock".
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
    // Grabs keyboard focus the first time this component is actually shown.
    // Before SynthPanel existed, MainComponent's own background showed
    // through the gaps between the throwaway sliders, so a click there
    // landed directly on MainComponent (which wants focus) and grabbed it
    // for free. SynthPanel now covers the entire window at 1:1 scale, so
    // EVERY click lands on the panel or one of its children - none of which
    // want focus - and a click can no longer grab it by accident. Without
    // this override the computer keyboard would never play a note again,
    // silently: keyStateChanged simply never fires.
    void visibilityChanged() override;


    //==============================================================================
    // Fires on the MIDI THREAD - not the message thread, and not the audio
    // thread. That's precisely why NoteRouter keeps a second FIFO: each queue
    // is strictly single-producer, and this is a different producer from the
    // QWERTY/on-screen path.
    void handleIncomingMidiMessage (juce::MidiInput* source,
                                     const juce::MidiMessage& message) override;

    // First-launch-only fallback: enables everything currently plugged in.
    // Once a device state has been saved (see appProperties below), restoring
    // it via setAudioChannels' XmlElement argument restores exactly the MIDI
    // inputs that were enabled last session instead, and this is skipped.
    void enableAllMidiInputs();

    // Opens JUCE's device selector. Without it the app just takes whatever
    // JUCE defaults to - on Windows that's WASAPI shared mode, whose latency
    // is high enough to get in the way of playing. ASIO is compiled in
    // (JUCE_ASIO), so an ASIO driver can be selected here. Also carries the
    // MIDI input device list. The button that fires this now lives on
    // SynthPanel; it only reports the click (panel.onAudioSettingsClicked),
    // since the panel has no reason to reach for the AudioDeviceManager.
    void showAudioSettings();

private:
    //==============================================================================
    // Persists deviceManager's audio/MIDI setup across restarts - see
    // documents/TODO.md's "Remember audio/MIDI device settings" item.
    // Loaded in the constructor, saved in the destructor.
    juce::ApplicationProperties appProperties;

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

    // A second peer, exactly like arp: owns its OWN step clock, gate and
    // pattern-index arithmetic, and decides WHICH note happens WHEN from
    // VoiceParameters' pattern storage. Item 7 build step 4
    // (documents/step-sequencer-design.md section 6) wires this into the
    // real 3-way hand-over below - arp and sequencer are a mutually exclusive
    // note source, never both driving at once.
    StepSequencer sequencer;

    // AUDIO-THREAD-PRIVATE. Edge-detects arpEnabled/seqEnabled together, since
    // switching either on or off is a HAND-OVER of the voice between three
    // possible owners and all three sides have to be told - see
    // documents/arpeggiator-design.md section 7 and
    // documents/step-sequencer-design.md sections 6 and 7. Replaced the
    // original two-way arpWasOn bool when item 7 build step 4 added the
    // sequencer as a third owner.
    VoiceOwner voiceOwner = VoiceOwner::Keys;

    // The real instrument panel, at its fixed design size - see
    // documents/ui-design.md sections 3 and 5. Declared after voice and
    // router: its constructor takes voice.getParameters() by reference and
    // its note-event callback closes over router, so both must already
    // exist. resized() below is the only place this class knows the panel's
    // size differs from the window's.
    SynthPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
