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
    // decision Stage B has to live with. Confirmed by step 1's canvas
    // mockup. designHeight grew from 660 in item 7 build step 6, exactly the
    // resize documents/step-sequencer-design.md section 9 flagged as
    // expected rather than a Polish-step afterthought - the real
    // SEQUENCER control cluster plus the 16-cell pattern grid replacing the
    // old 90px reserved strip needed real room. Flagged, not silent: this
    // moves the panel's aspect ratio further from the Pixel's 2.22:1
    // landscape shape than ui-design.md section 3's own letterboxing
    // discussion already worried about at 660 - the Android-port cost
    // question CLAUDE.md leaves open is unaffected in kind, only in degree.
    static constexpr int designWidth = 1280;
    static constexpr int designHeight = 840;

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

    //==========================================================================
    // Item 7 build step 6: one step of the 16-step pattern grid, replacing
    // the item-7 placeholder that used to live here (documents/ui-design.md
    // section 9's dim "reserved" outline). Hand-painted, following PianoKey's
    // precedent above, for the states no existing widget covers: gate/
    // accent/slide as flat colour/marker states, plus a continuous-value
    // overlay for whichever lane is currently selected for editing
    // (documents/step-sequencer-design.md section 9's bar-height/
    // fill-amount idea).
    //
    // GESTURES - this build step's own choice, since section 9 explicitly
    // leaves the exact gesture open ("informed by how it actually feels to
    // use", not a desk decision) - easy to revise if it reads wrong in play:
    //   - plain click (no drag): toggle Gate On/Off.
    //   - right-click: toggle Accent.
    //   - shift+click: toggle Slide.
    //   - vertical drag: adjust the CURRENTLY SELECTED lane's value for this
    //     step - quantised semitones for Pitch, a proportional 0..1 change
    //     for Cutoff/Resonance.
    // The three click gestures are lane-INDEPENDENT - switching which lane
    // is selected never changes what a plain click, right-click or
    // shift-click does, only what a DRAG does and what the overlay shows.
    //
    // Not wired through attachKnob/attachChoice/attachToggle - those
    // configure a single widget's own onChange callback, and this component
    // multiplexes several gestures onto one mouse listener instead. Uses
    // ParameterControls.h's loadStepValue/storeStepValue/loadStepFlag/
    // toggleStepFlag for the read/write side.
    struct StepCell final : public juce::Component
    {
        // Set by StepGrid::configure() right after construction, mirroring
        // PianoKeyboard's own "default-construct, then configure" precedent
        // (std::array needs default-constructible elements). Non-owning:
        // parameters and selectedLane both outlive every StepCell, since
        // StepGrid owns the array holding this cell and is destroyed
        // no earlier than the VoiceParameters/SynthPanel that own it.
        VoiceParameters* parameters = nullptr;
        int index = 0;
        const int* selectedLane = nullptr; // 0 = Pitch, 1 = Cutoff, 2 = Resonance

        // Set by StepGrid's Timer callback only - not this cell's own
        // concern to compute, since "which step is playing" is a single
        // grid-wide fact, not a per-cell one.
        bool isCurrentlyPlaying = false;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        // A drag shorter than this is a click, not a gesture - checked in
        // mouseUp via isDraggedFar rather than trusting JUCE's own
        // mouseWasDraggedSinceMouseDown flag, since that flag apparently
        // marks any nonzero movement as a drag, and a mouse click is never
        // perfectly still for a whole pixel-level of travel in practice.
        static constexpr float dragThreshold = 4.0f;

        // Drag-start snapshot, so a drag computes its delta from where the
        // gesture BEGAN rather than compounding per-callback rounding.
        // Which of the three lanes dragStartValue was read from is whatever
        // *selectedLane resolved to at mouseDown - if it changes mid-drag
        // (the combo box cannot itself take focus away from an in-progress
        // drag, but written down as a known limitation) the drag keeps
        // acting on the lane it started with.
        float dragStartValue = 0.0f;
        int dragStartY = 0;
        bool isDraggedFar = false;
        bool pendingRightClick = false;
        bool pendingShiftClick = false;
    };

    // 16 StepCells in a row, no per-cell caption - the lane-select and the
    // currently-playing highlight are grid-wide concerns, not something a
    // caption above each cell could show. Styled like PanelSection's own
    // rounded-rect fill/outline for visual consistency, but not built on
    // that class - StepCell has no separate caption strip, unlike every
    // existing KnobCell/ChoiceCell, so PanelSection's two-row grid geometry
    // does not fit it.
    struct StepGrid final : public juce::Component,
                             private juce::Timer
    {
        StepGrid();
        ~StepGrid() override;

        // Must be called once, before this component is shown - mirrors
        // PianoKeyboard's key-configuring loop in SynthPanel's constructor,
        // since std::array<StepCell, N> needs default-constructible
        // elements and configuration happens after.
        void configure (VoiceParameters& parametersToControl) noexcept;

        void paint (juce::Graphics&) override;
        void resized() override;

        // 0 = Pitch, 1 = Cutoff, 2 = Resonance. UI-local state, not a
        // VoiceParameters atomic - this selects which value a DRAG on any
        // cell edits and which overlay is drawn, it is not itself a synth
        // parameter. Shared with every StepCell via a pointer
        // (StepCell::selectedLane) set in configure(), so changing it once
        // here is visible to all 16 cells at once.
        int selectedLane = 0;

    private:
        // Polls VoiceParameters::currentStepForUi (audio thread -> UI) and
        // repaints only the two cells whose isCurrentlyPlaying flag actually
        // changes - see that atomic's own comment in VoiceParameters.h for
        // why this indirection exists at all rather than a direct read in
        // each cell's paint().
        void timerCallback() override;

        static constexpr int highlightHz = 30; // comfortably above a 16th note at any playable tempo

        std::array<StepCell, seqMaxSteps> cells;
        VoiceParameters* parameters = nullptr;
        int lastHighlightedStep = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
    };

    //==========================================================================
    // Cell counts per section - documents/ui-design.md section 2's table.
    static constexpr int numVcoKnobs = 5, numVcfKnobs = 3;
    static constexpr int numEnvKnobs = 4, numEnvChoices = 1;
    static constexpr int numLfoKnobs = 3, numLfoChoices = 1;
    static constexpr int numKeyboardKnobs = 1, numKeyboardChoices = 2;
    static constexpr int numArpKnobs = 2, numArpChoices = 2, numArpToggles = 2;
    static constexpr int numOutputKnobs = 1;

    // Item 7 build step 6's own SEQUENCER control cluster - not part of
    // documents/ui-design.md section 2's original table (that document is
    // item 6's spec, frozen before item 7 existed), so deliberately NOT
    // folded into the static_asserts below: those guard item 6's own
    // historical counts, not a running total of every control ever added to
    // the panel. On (toggle), Pattern Length and Lane are all special-cased
    // in the .cpp rather than routed through ChoiceSpec/attachChoice,
    // mirroring how the arp's On+Hold toggle stack and ENV's
    // envRowWidthCompensation cell are already special-cased there:
    //   - Lane has no VoiceParameters target at all (see
    //     StepGrid::selectedLane's own comment).
    //   - Pattern Length DOES have one (seqPatternLength), but
    //     attachChoice's generic contract stores the selected item's
    //     ZERO-BASED INDEX verbatim - correct for every other ChoiceSpec in
    //     this file because their target is an enum whose underlying value
    //     already IS that index, but seqPatternLength is a plain 1..16
    //     COUNT, not an enum, so index and value are off by exactly one.
    //     Wiring it by hand stores getSelectedId() itself instead.
    static constexpr int numSeqKnobs = 2;    // Tempo, Gate
    static constexpr int numSeqChoices = 1;  // Division only - see above
    static constexpr int numSeqToggles = 1;  // On (Hold has no seq equivalent)

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

    static const KnobSpec seqKnobSpecs[numSeqKnobs];
    static const ChoiceSpec seqChoiceSpecs[numSeqChoices];
    static const ToggleSpec seqToggleSpecs[numSeqToggles];

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

    // Item 7 build step 6, replacing the item-7 reserved placeholder that
    // used to be here. Cell order is On, Division, Pattern Length, Tempo,
    // Gate, Lane - matching documents/step-sequencer-design.md section 9's
    // "On/Division/Tempo/Gate/PatternLength" list with Pattern Length moved
    // next to Division (both structural/discrete) and Lane - this build
    // step's own addition, the grid's edit-mode selector - appended last.
    PanelSection seqControlSection { "SEQUENCER" };
    juce::ToggleButton seqOnToggle;
    juce::Label seqOnCaption; // blank - see arpToggleCaption's identical precedent above
    std::array<ChoiceCell, numSeqChoices> seqChoices; // Division only
    std::array<KnobCell, numSeqKnobs> seqKnobs;       // Tempo, Gate

    // NOT ChoiceSpec/attachChoice cells - see numSeqChoices' own comment
    // above for why each of these needs hand-wiring rather than the generic
    // helper every other combo box in this file goes through.
    juce::ComboBox seqPatternLengthCombo;
    juce::Label seqPatternLengthLabel;
    juce::ComboBox seqLaneCombo;
    juce::Label seqLaneLabel;

    StepGrid stepGrid;

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
