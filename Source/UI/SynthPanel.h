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
    // mockup. designHeight grew 660 -> 840 in item 7 build step 6, exactly
    // the resize documents/step-sequencer-design.md section 9 flagged as
    // expected rather than a Polish-step afterthought - the real
    // SEQUENCER control cluster plus the 16-cell pattern grid replacing the
    // old 90px reserved strip needed real room. Flagged, not silent: this
    // moves the panel's aspect ratio further from the Pixel's 2.22:1
    // landscape shape than ui-design.md section 3's own letterboxing
    // discussion already worried about at 660 - the Android-port cost
    // question CLAUDE.md leaves open is unaffected in kind, only in degree.
    //
    // Both constants moved several times across tempo-sync-design.md's build
    // and its UI follow-ups. designWidth: 1280 -> 1456 (LFO's two new cells
    // widened row B - "grow the canvas, don't rob the row's other sections",
    // same principle as designHeight's own item-7 growth above) -> 1252 once
    // OUTPUT left row B (first for its own row, then for SEQUENCER's),
    // shrinking row B back to 3 sections and 1212px raw, same as row A -
    // envRowWidthCompensation's own comment has the exact arithmetic; stable
    // at 1252 since (SEQUENCER's row absorbed OUTPUT without needing more
    // width - that row was never budgeted to designWidth in the first
    // place). designHeight: 840 -> 1004 when OUTPUT briefly had its own row
    // (one PanelSection::heightForCells(), 152px, plus its trailing gap,
    // 12px) -> back to 840 once OUTPUT joined SEQUENCER's existing row
    // instead of needing a new one.
    static constexpr int designWidth = 1252;
    static constexpr int designHeight = 840;

    // pushNoteEventIn is router.pushUiEvent, handed in by MainComponent in
    // step 6 - see the class comment above.
    SynthPanel (VoiceParameters& parametersToControl, std::function<void (const NoteEvent&)> pushNoteEventIn);
    ~SynthPanel() override;

    // Audio Settings opens JUCE's device selector, which needs the
    // AudioDeviceManager MainComponent owns - this panel has no reason to
    // reach for that itself, so it only reports the click. Wired in step 6.
    std::function<void()> onAudioSettingsClicked;

    // Save/Load open Source/Presets' dialogs, which need Arpeggiator (for the
    // latch snapshot, documents/settings-persistence-design.md section 5) -
    // MainComponent owns that, not this panel, so - same reasoning as
    // onAudioSettingsClicked above - these only report the click.
    std::function<void()> onSavePresetClicked;
    std::function<void()> onLoadPresetClicked;

    // The OUTPUT panel's Octave knob drags VoiceParameters::masterOctaveShift
    // directly - the shared, global octave transpose, same shift comma/
    // period drive from QwertyNoteInput. Called by MainComponent whenever
    // that shift changes via comma/period, so the knob's position stays in
    // sync with whichever input caused it.
    void refreshOctaveReadout();

    // Pushes every widget's displayed value back into sync with whatever
    // `params` currently holds - the read-back counterpart to the
    // constructor's one-time attachKnob/attachChoice/attachToggle wiring.
    // Needed after a preset load (documents/settings-persistence-design.md):
    // fromXml writes straight into the VoiceParameters atomics, which is
    // exactly correct for the audio thread but leaves every slider/combo/
    // toggle on this panel showing whatever it displayed before the load,
    // silently wrong - found via a user bug report (Load not lighting the
    // SEQUENCER On toggle even though the sequencer was, correctly, about to
    // run). Called by MainComponent after Tier A's startup restore and after
    // a Tier B Load.
    void refreshControlsFromParameters();

    void paint (juce::Graphics&) override;
    void resized() override;

    // documents/settings-persistence-design.md section 4's generic
    // enumeration: visits every KnobSpec/ChoiceSpec/ToggleSpec-covered field
    // exactly once, each under a key unique across the whole instrument, so
    // Source/Presets/PresetSerialization can save/load them without a
    // second, independently-drifting field list. A future rename to one of
    // the spec tables below (the kind tempo-sync-design.md's masterTempoBpm
    // merge already made) keeps the serializer in sync automatically - no
    // second edit site.
    //
    // Section 4 proposed a `serializedName` field on every one of the ~33
    // entries; this instead prefixes each key with the NAME OF THE TABLE the
    // entry came from ("arp.Gate", "seq.Gate", ...) - same uniqueness
    // guarantee (a display-name collision like the two "Gate" knobs or the
    // three-table "Division"/"On" ones below never merges two different
    // fields, since it would need a collision on BOTH the table AND the
    // display name, and no table repeats a display name internally today),
    // with no new field to type - and risk forgetting - on every existing
    // initializer. See the .cpp for the full table list.
    static void forEachSerializableParameter (
        const std::function<void (const juce::String& key, std::atomic<float> VoiceParameters::*)>& onFloat,
        const std::function<void (const juce::String& key, std::atomic<int> VoiceParameters::*)>& onInt);

private:
    //==========================================================================
    // Autoviji's "surprise me" handler - fills the pattern with a random
    // note per step (two fixed octaves), rolls each step's gate (1-in-8
    // off) and per-step Cutoff lane, then turns the sequencer on. Wired to
    // autovijiButton.onClick in the constructor. See documents/TODO.md
    // item 8 and documents/autoviji-design.md.
    void randomizeSequence();

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
    // label the way a KnobCell/ChoiceCell does. Reused as-is by SEQUENCER's
    // own On/Record pair (item 7 build step 7) - same shape, a second
    // independent-ish on/off switch sharing conceptual space with the first.
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
    // numLfoChoices/numLfoToggles grew by tempo-sync-design.md's build step 4:
    // Sync Division (a new ChoiceSpec, reusing arpDivisionChoices) and Sync
    // (LFO's first-ever toggle, a plain ToggleButton in its own cell - not a
    // stack, see lfoSyncToggle's own comment below).
    static constexpr int numLfoKnobs = 3, numLfoChoices = 2, numLfoToggles = 1;
    static constexpr int numKeyboardKnobs = 1, numKeyboardChoices = 2;
    // numArpKnobs shrank / numOutputKnobs grew: Tempo moved out of ARP to
    // OUTPUT, next to Level - see outputKnobSpecs' own comment in the .cpp.
    static constexpr int numArpKnobs = 1, numArpChoices = 2, numArpToggles = 2;
    static constexpr int numOutputKnobs = 2;

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
    static constexpr int numSeqKnobs = 1;    // Gate only - Tempo knob removed,
                                              // see tempo-sync-design.md
    static constexpr int numSeqChoices = 1;  // Division only - see above
    static constexpr int numSeqToggles = 2;  // On, Record (item 7 build step 7) -
                                              // stacked into ONE cell via
                                              // ToggleStack, same as the arp's
                                              // On+Hold, so - like numArpToggles -
                                              // this constant sizes seqToggleSpecs
                                              // only, and is deliberately NOT what
                                              // the resized() cell-width formula
                                              // uses for this pair; see that
                                              // formula's own comment.

    // Item 10's CHARACTER section (documents/character-and-vim.md's "V1
    // build scope" section) - same "not part of item 6's original table"
    // status as numSeqKnobs/numSeqChoices/numSeqToggles above, deliberately
    // NOT folded into the static_asserts below for the same reason. 3 cells
    // total: the VIM/Chorus toggle stack, Drive, Humanise - see the doc's
    // own settled-scope table for why those three and not the full spec's
    // ~19-cell list.
    static constexpr int numCharacterKnobs = 2;    // Drive, Humanise
    static constexpr int numCharacterToggles = 2;  // Vim, Chorus - stacked into ONE
                                                    // cell via ToggleStack, same
                                                    // "hardcode 1 cell, not the
                                                    // toggle count" precedent as
                                                    // numArpToggles/numSeqToggles.

    // The load-bearing counts from section 2: "19 knobs, 6 combo boxes, 2
    // toggles = 27" must still hold after being spread across seven arrays
    // instead of one flat table - a dropped parameter looks completely
    // normal (section 6.2), so this is asserted rather than trusted by eye.
    static_assert (numVcoKnobs + numVcfKnobs + numEnvKnobs + numLfoKnobs
                       + numKeyboardKnobs + numArpKnobs + numOutputKnobs == 19,
                   "19 knobs total - documents/ui-design.md section 2");
    // 7, not item 6's original 6 - documents/tempo-sync-design.md's LFO Sync
    // Division combo is the one addition since ui-design.md section 2 was
    // written.
    static_assert (numEnvChoices + numLfoChoices + numKeyboardChoices + numArpChoices == 7,
                   "7 combo boxes total - documents/ui-design.md section 2's original 6, "
                   "plus tempo-sync-design.md's LFO Sync Division combo");

    // 3, not item 6's original 2 - documents/tempo-sync-design.md's LFO Sync
    // toggle is the one addition (item 7's own seqToggleStack pair stays
    // excluded, same reasoning as numSeqToggles' own comment above).
    static_assert (numArpToggles + numLfoToggles == 3,
                   "3 toggles total - documents/ui-design.md section 2's original 2 (arp "
                   "On/Hold), plus tempo-sync-design.md's LFO Sync toggle");

    // Row A (VCO|VCF|ENV) and row B (LFO|KEYBOARD|ARP) both have 3 sections
    // now, so this constant is back to 0 - the width math (below) happens to
    // land exactly even without spending anything on ENV's Destination cell.
    // Non-zero at two earlier points, both since undone: originally 28px
    // (item 6: row B had a 4th section, OUTPUT, that row A had no
    // equivalent for); briefly 204px after tempo-sync-design.md's LFO growth
    // widened row B to 1416px while row B still had 4 sections. OUTPUT
    // moving to its own row under ARP (outputSection's own comment) dropped
    // row B back to 3 sections, and by coincidence row A's own 13 knob/
    // choice cells across 3 sections (5+3+5) exactly match row B's new 13
    // (6+3+4) - same section count, same total cell count, so the two
    // widthForCells sums are identical with no help needed. Left in place
    // (not deleted) rather than assumed permanent - the mechanism is one
    // edit away if either row's cell count changes again.
    static constexpr int envRowWidthCompensation = 0;

    static const KnobSpec vcoKnobSpecs[numVcoKnobs];
    static const KnobSpec vcfKnobSpecs[numVcfKnobs];
    static const KnobSpec envKnobSpecs[numEnvKnobs];
    static const ChoiceSpec envChoiceSpecs[numEnvChoices];
    static const KnobSpec lfoKnobSpecs[numLfoKnobs];
    static const ChoiceSpec lfoChoiceSpecs[numLfoChoices];
    static const ToggleSpec lfoToggleSpecs[numLfoToggles];
    static const KnobSpec keyboardKnobSpecs[numKeyboardKnobs];
    static const ChoiceSpec keyboardChoiceSpecs[numKeyboardChoices];
    static const KnobSpec arpKnobSpecs[numArpKnobs];
    static const ChoiceSpec arpChoiceSpecs[numArpChoices];
    static const ToggleSpec arpToggleSpecs[numArpToggles];
    static const KnobSpec outputKnobSpecs[numOutputKnobs];

    static const KnobSpec seqKnobSpecs[numSeqKnobs];
    static const ChoiceSpec seqChoiceSpecs[numSeqChoices];
    static const ToggleSpec seqToggleSpecs[numSeqToggles];

    static const KnobSpec characterKnobSpecs[numCharacterKnobs];
    static const ToggleSpec characterToggleSpecs[numCharacterToggles];

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

    // Sync toggle cell goes first (documents/tempo-sync-design.md build step
    // 4), same "toggle cell first" precedent as ARP/SEQUENCER below - a
    // plain juce::ToggleButton in its own cell, not a ToggleStack: LFO gains
    // exactly one toggle, so a 2-slot stack doesn't fit and a new stack
    // variant for exactly one button isn't worth inventing. lfoSyncCaption
    // is blank, existing purely to satisfy addCell's two-component contract -
    // same as arpToggleCaption/seqToggleCaption above.
    PanelSection lfoSection { "LFO" };
    juce::Label lfoSyncCaption;
    juce::ToggleButton lfoSyncToggle;
    std::array<KnobCell, numLfoKnobs> lfoKnobs;
    std::array<ChoiceCell, numLfoChoices> lfoChoices; // Waveform, Sync Division

    PanelSection keyboardSection { "KEYBOARD" };
    std::array<KnobCell, numKeyboardKnobs> keyboardKnobs;
    std::array<ChoiceCell, numKeyboardChoices> keyboardChoices;

    // Cell order here is On+Hold, Pattern, Division, Gate - matching
    // documents/ui-design.md section 2's table (minus Tempo, moved to OUTPUT
    // - see outputKnobs' own comment below), and the order addCell is called
    // in the constructor.
    PanelSection arpSection { "ARPEGGIATOR" };
    ToggleStack arpToggleStack;
    juce::Label arpToggleCaption; // blank - see the ToggleStack comment above
    std::array<ChoiceCell, numArpChoices> arpChoices;
    std::array<KnobCell, numArpKnobs> arpKnobs;

    // Tempo moved here from ARP - masterTempoBpm reads globally (arp,
    // sequencer, synced LFO), so OUTPUT's plain global-controls cluster
    // reads truer than a section named for one of its three consumers.
    // Octave joins it for the same reason (documents/note-handling-design.md
    // section 7's revision) - it now reaches Keys/Arp/Seq alike, not just
    // the on-screen keyboard it used to sit next to.
    PanelSection outputSection { "OUTPUT" };
    std::array<KnobCell, numOutputKnobs> outputKnobs;

    // Item 7 build step 6, replacing the item-7 reserved placeholder that
    // used to be here. Cell order is On+Record, Division, Pattern Length,
    // Tempo, Gate, Lane - matching documents/step-sequencer-design.md
    // section 9's "On/Division/Tempo/Gate/PatternLength" list with Pattern
    // Length moved next to Division (both structural/discrete) and Lane -
    // build step 6's own addition, the grid's edit-mode selector - appended
    // last. Build step 7 turns the bare On toggle into an On+Record
    // ToggleStack, mirroring arpToggleStack exactly, rather than adding a
    // whole new cell - see numSeqToggles' own comment above.
    PanelSection seqControlSection { "SEQUENCER" };
    ToggleStack seqToggleStack;
    juce::Label seqToggleCaption; // blank - see arpToggleCaption's identical precedent above
    std::array<ChoiceCell, numSeqChoices> seqChoices; // Division only
    std::array<KnobCell, numSeqKnobs> seqKnobs;       // Gate

    // NOT ChoiceSpec/attachChoice cells - see numSeqChoices' own comment
    // above for why each of these needs hand-wiring rather than the generic
    // helper every other combo box in this file goes through.
    juce::ComboBox seqPatternLengthCombo;
    juce::Label seqPatternLengthLabel;
    juce::ComboBox seqLaneCombo;
    juce::Label seqLaneLabel;

    // Item 10 (documents/character-and-vim.md) - v1 scope, 3 cells, sitting
    // between SEQUENCER and OUTPUT on their shared row (that doc's own "V1
    // build scope" section has the width-budget arithmetic). Cell order is
    // the VIM/Chorus toggle stack, Drive, Humanise - same "toggle cell goes
    // first" precedent as ARP/SEQUENCER above.
    PanelSection characterSection { "CHARACTER" };
    ToggleStack characterToggleStack;
    juce::Label characterToggleCaption; // blank - see arpToggleCaption's identical precedent above
    std::array<KnobCell, numCharacterKnobs> characterKnobs;

    StepGrid stepGrid;

    PianoKeyboard pianoKeyboard;

    // Lives in outputSection (addCell'd alongside Level/Tempo), not
    // freestanding next to pianoKeyboard - documents/note-handling-design.md
    // section 7's revision: a global "note output" transpose belongs with
    // OUTPUT's other global controls, not next to one input source. A plain
    // KnobCell, hand-wired in the constructor rather than through
    // wireKnobs/KnobSpec - masterOctaveShift is an atomic<int> stepped over
    // 7 whole-octave positions with a signed-integer text readout, neither of
    // which KnobSpec's float-continuous/numeric-suffix contract covers, same
    // "doesn't fit the generic shape, hand-wire it" precedent as
    // seqPatternLengthCombo/seqLaneCombo above.
    KnobCell octaveKnob;

    juce::TextButton audioSettingsButton;

    // Item 8 (documents/TODO.md) - header row, left of Audio Settings, not
    // a SEQUENCER cell (that section's width stays exactly what item 7 left
    // it at). Plain action button, same onClick-only shape as
    // audioSettingsButton - no persistent toggle state, just an onClick that
    // calls randomizeSequence().
    juce::TextButton autovijiButton;

    // Item 9 (documents/settings-persistence-design.md section 6) - header
    // row, between autovijiButton and audioSettingsButton. Plain
    // onClick-only buttons, same shape as audioSettingsButton/autovijiButton
    // above: each only reports its click (onSavePresetClicked/
    // onLoadPresetClicked), since the dialog they open needs Arpeggiator,
    // which this panel doesn't own.
    juce::TextButton savePresetButton, loadPresetButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPanel)
};
