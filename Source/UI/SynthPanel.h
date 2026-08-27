#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/NoteEvent.h"
#include "../DSP/VoiceParameters.h"
#include "PanelLookAndFeel.h"
#include "PanelSection.h"
#include "ParameterControls.h"

//==============================================================================
/*
    Step 5 of documents/ui-design.md: the real instrument panel, all seven
    sections plus the piano-style keyboard row, Audio Settings button and
    the empty SEQ strip, composed at the FIXED design size (section 3).

    Takes VoiceParameters& and a note-event sink rather than reaching into
    SynthVoice/NoteRouter itself (section 5, "SynthPanel's interface") - the
    same reasoning as router.pushUiEvent being handed to the throwaway
    scaffolding's callbacks rather than this class calling router directly.

    resized() lays out against designWidth/designHeight ONLY - it never reads
    getWidth()/getHeight(). The scale transform that maps that fixed layout
    onto the real window is step 6's job (MainComponent), not this class's.
*/
class SynthPanel final : public juce::Component
{
public:
    // documents/ui-design.md section 3 - the one place item 6 makes a
    // decision Stage B has to live with. Confirmed by step 1's canvas mockup.
    static constexpr int designWidth = 1280;
    static constexpr int designHeight = 660;

    // pushNoteEventIn is router.pushUiEvent, handed in by MainComponent in
    // step 6 - see the class comment above.
    SynthPanel (VoiceParameters& parametersToControl, std::function<void (const NoteEvent&)> pushNoteEventIn);
    ~SynthPanel() override;

    // Audio Settings opens JUCE's device selector, which needs the
    // AudioDeviceManager MainComponent owns - this panel has no reason to
    // reach for that itself, so it only reports the click. Wired in step 6.
    std::function<void()> onAudioSettingsClicked;

    // The on-screen Octave Up/Down buttons only report the click - the panel
    // has no reason to reach for QwertyNoteInput itself, same as
    // onAudioSettingsClicked above not reaching for the AudioDeviceManager.
    // Wired in MainComponent, which owns QwertyNoteInput.
    std::function<void()> onOctaveUpClicked;
    std::function<void()> onOctaveDownClicked;

    // Called by MainComponent whenever QwertyNoteInput's octave shift
    // changes - by an Octave button click OR by comma/period - so the
    // on-screen keyboard's notes and its readout stay in sync with whichever
    // caused it. See octaveShift's member comment.
    void setOctaveShift (int newShift);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    //==========================================================================
    // One knob/choice cell each - the same shape as the throwaway
    // scaffolding's DebugControl/DebugChoice (MainComponent.h), generalised
    // across all seven sections instead of one flat table.
    // ParameterControls.h's attach helpers own all real configuration; these
    // structs only exist so a widget and its caption travel together.
    struct KnobCell   { juce::Slider slider; juce::Label label; };
    struct ChoiceCell { juce::ComboBox comboBox; juce::Label label; };

    // Arp On/Hold share one cell (documents/ui-design.md section 2) - two
    // ToggleButtons stacked, each drawing its own caption via
    // PanelLookAndFeel::drawToggleButton, so the cell needs no separate
    // label the way a KnobCell/ChoiceCell does.
    struct ToggleStack final : public juce::Component
    {
        juce::ToggleButton top, bottom;

        // Without this, top/bottom are never actually CHILDREN of this
        // component - resized() would still set their bounds, but a
        // Component that was never added via addAndMakeVisible neither
        // paints nor receives clicks. Easy to miss for exactly this reason:
        // nothing here fails to compile, it just silently draws nothing.
        ToggleStack()
        {
            addAndMakeVisible (top);
            addAndMakeVisible (bottom);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            top.setBounds (area.removeFromTop (area.getHeight() / 2));
            bottom.setBounds (area);
        }
    };

    // The empty item-7 placeholder (documents/ui-design.md section 9) -
    // dim outline and caption, nothing interactive, nothing p-lock-shaped.
    struct SeqReservedStrip final : public juce::Component
    {
        void paint (juce::Graphics&) override;
    };

    //==========================================================================
    // Cell counts per section - documents/ui-design.md section 2's table.
    static constexpr int numVcoKnobs = 5, numVcfKnobs = 3;
    static constexpr int numEnvKnobs = 4, numEnvChoices = 1;
    static constexpr int numLfoKnobs = 3, numLfoChoices = 1;
    static constexpr int numKeyboardKnobs = 1, numKeyboardChoices = 2;
    static constexpr int numArpKnobs = 2, numArpChoices = 2, numArpToggles = 2;
    static constexpr int numOutputKnobs = 1;

    // The load-bearing counts from section 2: "19 knobs, 6 combo boxes, 2
    // toggles = 27" must still hold after being spread across seven arrays
    // instead of one flat table - a dropped parameter looks completely
    // normal (section 6.2), so this is asserted rather than trusted by eye.
    static_assert (numVcoKnobs + numVcfKnobs + numEnvKnobs + numLfoKnobs
                       + numKeyboardKnobs + numArpKnobs + numOutputKnobs == 19,
                   "19 knobs total - documents/ui-design.md section 2");
    static_assert (numEnvChoices + numLfoChoices + numKeyboardChoices + numArpChoices == 6,
                   "6 combo boxes total - documents/ui-design.md section 2");
    static_assert (numArpToggles == 2, "2 toggles total - documents/ui-design.md section 2");

    // Row A (VCO|VCF|ENV) has 3 sections against row B (LFO|KEYBOARD|ARP|
    // OUTPUT)'s 4, so it has one FEWER inter-section gap and falls short of
    // row B's total width by exactly one gap plus the missing section's own
    // edge padding: 1240px (row B, section 3's full usable width) minus
    // 1212px (row A's raw total) = 28px. Spent on ENV's Destination cell
    // (both the addCell and the widthForCells calls in the .cpp) rather than
    // a fourth, purely cosmetic section.
    static constexpr int envRowWidthCompensation = 28;

    static const KnobSpec vcoKnobSpecs[numVcoKnobs];
    static const KnobSpec vcfKnobSpecs[numVcfKnobs];
    static const KnobSpec envKnobSpecs[numEnvKnobs];
    static const ChoiceSpec envChoiceSpecs[numEnvChoices];
    static const KnobSpec lfoKnobSpecs[numLfoKnobs];
    static const ChoiceSpec lfoChoiceSpecs[numLfoChoices];
    static const KnobSpec keyboardKnobSpecs[numKeyboardKnobs];
    static const ChoiceSpec keyboardChoiceSpecs[numKeyboardChoices];
    static const KnobSpec arpKnobSpecs[numArpKnobs];
    static const ChoiceSpec arpChoiceSpecs[numArpChoices];
    static const ToggleSpec arpToggleSpecs[numArpToggles];
    static const KnobSpec outputKnobSpecs[numOutputKnobs];

    //==========================================================================
    // On-screen keyboard (C3-C4), piano-styled per documents/ui-mockup's
    // .kbd. `letter` is the QWERTY key that plays the same note
    // (QwertyNoteInput.h's own base-row mapping - Z through Q, same order),
    // shown under the keyboard so it's clear which computer key to press.
    struct KeyboardKeySpec { const char* name; int semitoneOffset; bool isBlackKey; char letter; };
    static constexpr int numKeyboardKeys = 13;
    static constexpr int keyboardBaseNoteNumber = 48; // C3, matches QWERTY's base
    static const KeyboardKeySpec keyboardKeySpecs[numKeyboardKeys];

    // One piano key - white or black, custom-painted (see the .cpp) rather
    // than using TextButton's own look, since a real key's shape (flat top,
    // rounded bottom only) and the white/black overlap documents/ui-mockup
    // calls for aren't expressible through PanelLookAndFeel's
    // drawButtonBackground. Extends MomentaryButton for its press/release
    // semantics only - paint() ignores everything TextButton would
    // otherwise draw.
    struct PianoKey final : public MomentaryButton
    {
        bool isBlackKey = false;
        juce::String noteName;

        // The note number actually EMITTED when this key went down, reused
        // for the matching note-off rather than recomputed - same reasoning
        // as QwertyNoteInput::KeyState::emittedNoteNumber (see that header):
        // recomputing from the current octaveShift at release time would
        // break if the octave shifts while the key is physically still held.
        std::uint8_t emittedNoteNumber = 0;

        void paint (juce::Graphics&) override;
    };

    // The on-screen keyboard as a whole: 8 white keys evenly filling the
    // row, 5 black keys overlapping them at fixed positions (documents/
    // ui-mockup's .kbd/.wk/.bk percentages), plus a row of QWERTY-letter
    // captions underneath, X-aligned to each key by construction since
    // resized() computes both from the same per-key bounds.
    struct PianoKeyboard final : public juce::Component
    {
        static constexpr int letterRowHeight = 16;
        static constexpr int letterGap = 4;

        std::array<PianoKey, numKeyboardKeys> keys;
        std::array<juce::Label, numKeyboardKeys> letters;

        PianoKeyboard();

        void paint (juce::Graphics&) override;
        void resized() override;
    };

    // Octave Up/Down, to the left of the on-screen keyboard, plus a readout
    // of the octave that base note C currently sits in (matches
    // keyboardBaseNoteNumber's own C3 convention - see that constant). Only
    // reports clicks upward via SynthPanel::onOctaveUpClicked/DownClicked;
    // SynthPanel::setOctaveShift is what actually moves the readout, so a
    // click and a comma/period press update it the exact same way.
    struct OctaveControl final : public juce::Component
    {
        juce::TextButton upButton { "+" }, downButton { "-" };

        // "." and "," under each button - the matching QWERTY shortcut.
        // Duplicates QwertyNoteInput's octaveUpKeyCode/octaveDownKeyCode as
        // plain display text rather than reaching into that private,
        // platform-varying constant (0xBE/0xBC on Windows, see that header) -
        // same trade-off PianoKeyboard's own `letter` captions already make
        // against QwertyNoteInput's key map.
        juce::Label upShortcut, downShortcut;

        juce::Label readout;

        OctaveControl();

        void resized() override;
    };

    //==========================================================================
    VoiceParameters& params;
    std::function<void (const NoteEvent&)> pushNoteEvent;

    // Set on THIS component in the constructor, not per-widget - JUCE
    // cascades a LookAndFeel to children that don't have their own, so every
    // knob/combo/toggle below inherits it automatically. Must be unset in
    // the destructor before this member is destroyed, or JUCE asserts on
    // shutdown (documents/ui-design.md section 7's note on this).
    PanelLookAndFeel lookAndFeel;

    // The app mark, matching documents/ui-mockup's header markup exactly -
    // "AVIJI<em>A</em>TOR". ONE component drawing a juce::AttributedString
    // with three colour runs, not three adjacent Labels - three separate
    // Labels each get their own independent text layout with no shared
    // kerning context, which is exactly what produced visible gaps around
    // the A; AttributedString shapes the whole word as one run and just
    // tints part of it.
    struct TitleMark final : public juce::Component
    {
        void paint (juce::Graphics&) override;
    };
    TitleMark titleMark;
    juce::Label tagLabel; // the tagline beside the mark, mockup's ".tag"

    PanelSection vcoSection { "VCO" };
    std::array<KnobCell, numVcoKnobs> vcoKnobs;

    PanelSection vcfSection { "VCF" };
    std::array<KnobCell, numVcfKnobs> vcfKnobs;

    PanelSection envSection { "ENV" };
    std::array<KnobCell, numEnvKnobs> envKnobs;
    std::array<ChoiceCell, numEnvChoices> envChoices;

    PanelSection lfoSection { "LFO" };
    std::array<KnobCell, numLfoKnobs> lfoKnobs;
    std::array<ChoiceCell, numLfoChoices> lfoChoices;

    PanelSection keyboardSection { "KEYBOARD" };
    std::array<KnobCell, numKeyboardKnobs> keyboardKnobs;
    std::array<ChoiceCell, numKeyboardChoices> keyboardChoices;

    // Cell order here is On+Hold, Pattern, Division, Tempo, Gate - matching
    // documents/ui-design.md section 2's table, and the order addCell is
    // called in the constructor.
    PanelSection arpSection { "ARPEGGIATOR" };
    ToggleStack arpToggleStack;
    juce::Label arpToggleCaption; // blank - see the ToggleStack comment above
    std::array<ChoiceCell, numArpChoices> arpChoices;
    std::array<KnobCell, numArpKnobs> arpKnobs;

    PanelSection outputSection { "OUTPUT" };
    std::array<KnobCell, numOutputKnobs> outputKnobs;

    SeqReservedStrip seqStrip;

    PianoKeyboard pianoKeyboard;
    OctaveControl octaveControl;

    // Drives the on-screen keyboard's note numbers (keyboardBaseNoteNumber +
    // octaveShift * 12 + semitoneOffset) - see setOctaveShift. NOT the source
    // of truth: that's QwertyNoteInput's own octaveShift, which MainComponent
    // mirrors in here after every change.
    int octaveShift = 0;

    juce::TextButton audioSettingsButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPanel)
};
