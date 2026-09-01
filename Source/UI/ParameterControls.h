#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DSP/VoiceParameters.h"
#include "PanelLookAndFeel.h"

//==============================================================================
/*
    documents/ui-design.md section 5's spec-table pattern, widened from the
    throwaway scaffolding's two spec kinds (MainComponent.cpp's
    DebugControlSpec/DebugChoiceSpec) to three. Same idea as those: one table
    row per control, one attach() call per row, so PanelSection (step 4) and
    SynthPanel (step 5) add a control by adding a row rather than more
    copy-paste.

    Every attach helper owns ALL of a widget's configuration - range, skew,
    suffix, the onValueChange/onChange callback, the keyboard-focus flag, and
    the seed call that fires the callback once at construction so the widget
    and its atomic cannot disagree at startup (easy to forget, silent when
    forgotten - see section 6.2). PanelSection's job is strictly layout: it
    positions the already-fully-configured widget, never restyles it.
*/

//==============================================================================
// A rotary knob. Covers all 19 of item 6's float controls, including the
// time-valued ones (Attack/Decay/Release, Glide) and Cutoff.
struct KnobSpec
{
    const char* name;
    double min, max, def;

    // Cutoff is the only control with storeAsLog2 = true. skewMidpoint is
    // orthogonal to that - Attack/Decay/Release/Glide want the skew (short
    // times aren't crammed into the first few pixels of travel) without
    // storing as log2, since VoiceParameters keeps their seconds values
    // linear. 0 = no skew.
    bool storeAsLog2;
    double skewMidpoint;

    const char* suffix; // e.g. " Hz", " s" - nullptr/"" for none
    std::atomic<float> VoiceParameters::* target;
};

// A combo box. Covers Envelope Destination, LFO Waveform, Glide Mode, Note
// Priority, Arp Pattern, Arp/Seq/LFO-Sync Division.
struct ChoiceSpec
{
    const char* name;
    const char* const* choices;
    int numChoices;

    // The TRUE, GLOBAL enum value of the default choice - never a UI-local
    // index, even when firstChoiceValue below is non-zero. Every existing
    // ChoiceSpec already followed this (their enum's index IS its value,
    // since firstChoiceValue was implicitly 0); this comment exists so a
    // sliced ChoiceSpec's author doesn't accidentally make defaultIndex
    // relative to `choices` instead.
    int defaultIndex;

    std::atomic<int> VoiceParameters::* target;

    // 0 for almost every ChoiceSpec: `choices` starts at the target enum's
    // own index 0, so the selected combo item's 0-based index already IS
    // the value to store. Non-zero when `choices` instead points PARTWAY
    // into a larger shared array (documents/tempo-sync-design.md's
    // Division combos, extended for arp/seq's "tedious below 1/1"/LFO's
    // "slow sweeps to 32/1" split - see StepDivision's own comment in
    // StepClock.h): firstChoiceValue is `choices[0]`'s true enum value, so
    // attachChoice can add it back to the combo's own 0-based selection
    // before storing, and subtract it back off when seeding from
    // defaultIndex. Proven by runAttachChoiceOffsetSelfTest below, not just
    // asserted - a backwards offset here is a SILENT wrong-division bug,
    // the same failure class CLAUDE.md flags for clock-adjacent DSP work.
    int firstChoiceValue = 0;
};

// A toggle. Covers Arp On and Arp Hold - two ComboBoxes in the throwaway
// scaffolding, two ToggleButtons here (documents/ui-design.md section 2).
// Both still store 0/1 into the same std::atomic<int>, so nothing downstream
// of VoiceParameters notices the widget change.
struct ToggleSpec
{
    const char* name;
    int defaultValue;
    std::atomic<int> VoiceParameters::* target;
};

//==============================================================================
// Configures `slider` and `label` from `spec`, wires the callback, and seeds
// the atomic. `label` is the caption above the knob (section 3's "16 label"
// strip) - the value readout below it is the slider's own text box, not a
// second Label.
inline void attachKnob (juce::Slider& slider, juce::Label& label,
                         const KnobSpec& spec, VoiceParameters& params)
{
    // Upper-cased for display only - spec.name itself stays normal case,
    // since that's what the tables above read as and any future debug
    // output would print.
    label.setText (juce::String (spec.name).toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (PanelLookAndFeel::captionFont());

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

    // 16px value-readout strip (documents/ui-design.md section 3); 70px wide,
    // matching the throwaway scaffolding's text box.
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    slider.setRange (spec.min, spec.max);

    if (spec.skewMidpoint > 0.0)
        slider.setSkewFactorFromMidPoint (spec.skewMidpoint);

    if (spec.suffix != nullptr && spec.suffix[0] != '\0')
        slider.setTextValueSuffix (spec.suffix);

    // Display only - the underlying value and drag precision are untouched.
    // Without this the readout shows a raw double's default ~7 decimal
    // places (e.g. "0.7000000"), which is noise nobody reads. Wide-range
    // controls (Cutoff, Arp Tempo) read better as whole numbers; everything
    // else gets 2 decimal places, which is as fine as any of these controls
    // are actually tuned by ear.
    slider.setNumDecimalPlacesToDisplay (spec.max - spec.min >= 50.0 ? 0 : 2);

    // Every mouse-driven control must decline keyboard focus, or clicking it
    // steals focus and silently stops the computer keyboard playing notes -
    // nothing logged, no crash. Combo boxes are the one deliberate exception
    // (attachChoice, below) since they genuinely need keys to operate.
    // documents/ui-design.md section 6.1.
    slider.setWantsKeyboardFocus (false);

    slider.onValueChange = [&slider, &spec, &params]
    {
        const auto value = (float) slider.getValue();
        (params.*(spec.target)).store (spec.storeAsLog2 ? std::log2 (value) : value,
                                        std::memory_order_relaxed);
    };

    slider.setValue (spec.def, juce::dontSendNotification);

    // Seed: the widget and the atomic cannot disagree at startup.
    slider.onValueChange();
}

// Configures `comboBox` and `label` from `spec`, wires onChange, and seeds
// the atomic.
inline void attachChoice (juce::ComboBox& comboBox, juce::Label& label,
                           const ChoiceSpec& spec, VoiceParameters& params)
{
    // Upper-cased for display only - see attachKnob's identical comment.
    label.setText (juce::String (spec.name).toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (PanelLookAndFeel::captionFont());

    // juce::ComboBox item IDs are 1-based - 0 means "no selection" - so
    // store id = choice index + 1, and subtract 1 back off when reading
    // getSelectedId(). Same convention as the throwaway scaffolding.
    for (int i = 0; i < spec.numChoices; ++i)
        comboBox.addItem (spec.choices[i], i + 1);

    // Deliberately NOT setWantsKeyboardFocus(false) - the one exception to
    // the focus-trap rule above, since a combo box needs keys to operate.

    comboBox.onChange = [&comboBox, &spec, &params]
    {
        const auto index = comboBox.getSelectedId() - 1 + spec.firstChoiceValue;
        (params.*(spec.target)).store (index, std::memory_order_relaxed);
    };

    // defaultIndex is always the TRUE global value (spec.defaultIndex's own
    // comment) - subtract firstChoiceValue back off here since setSelectedId
    // wants a combo-LOCAL id, the exact inverse of onChange's own +
    // firstChoiceValue above. Zero for every ChoiceSpec that isn't sliced
    // into a larger array, so this is `spec.defaultIndex + 1` exactly as
    // before for all of them.
    comboBox.setSelectedId (spec.defaultIndex - spec.firstChoiceValue + 1, juce::dontSendNotification);

    // Seed, matching attachKnob's pattern.
    comboBox.onChange();
}

// Configures `toggle` from `spec`, wires onClick, and seeds the atomic. No
// caption Label parameter, unlike the two above: a ToggleButton draws its
// own text (PanelLookAndFeel::drawToggleButton), so `spec.name` becomes the
// button's own text rather than a sibling Label.
inline void attachToggle (juce::ToggleButton& toggle, const ToggleSpec& spec, VoiceParameters& params)
{
    toggle.setButtonText (spec.name);
    toggle.setWantsKeyboardFocus (false);

    toggle.onClick = [&toggle, &spec, &params]
    {
        (params.*(spec.target)).store (toggle.getToggleState() ? 1 : 0, std::memory_order_relaxed);
    };

    toggle.setToggleState (spec.defaultValue != 0, juce::dontSendNotification);

    // Seed, matching the other two attach helpers.
    toggle.onClick();
}

//==============================================================================
/*
    Item 7 build step 6: the "index-based attach-helper" documents/
    step-sequencer-design.md section 4 calls for, alongside attachKnob/
    attachChoice/attachToggle above. Not an attach helper in the same sense
    as those three, though - StepCell (SynthPanel.h) is a hand-painted
    juce::Component with several gestures multiplexed onto one mouse
    listener, not a single Slider/ComboBox/ToggleButton with one onChange
    callback, so there is no widget for a spec+attach pair to configure.
    These four just do the read/write side these gestures need: a
    member-pointer-to-ARRAY plus a step index, rather than attachKnob's
    member-pointer-to-scalar.
*/
template <typename T>
inline T loadStepValue (std::array<std::atomic<T>, seqMaxSteps> VoiceParameters::* target,
                         int index, const VoiceParameters& params) noexcept
{
    return (params.*target)[(size_t) index].load (std::memory_order_relaxed);
}

template <typename T>
inline void storeStepValue (std::array<std::atomic<T>, seqMaxSteps> VoiceParameters::* target,
                             int index, T value, VoiceParameters& params) noexcept
{
    (params.*target)[(size_t) index].store (value, std::memory_order_relaxed);
}

// The array equivalent of ToggleSpec/attachToggle's 0/1 storage convention -
// stepGateOn/stepAccent/stepSlide are all std::atomic<int> arrays storing
// 0 or 1, same as every other boolean flag in VoiceParameters.
inline bool loadStepFlag (std::array<std::atomic<int>, seqMaxSteps> VoiceParameters::* target,
                           int index, const VoiceParameters& params) noexcept
{
    return loadStepValue (target, index, params) != 0;
}

inline void toggleStepFlag (std::array<std::atomic<int>, seqMaxSteps> VoiceParameters::* target,
                             int index, VoiceParameters& params) noexcept
{
    storeStepValue (target, index, loadStepFlag (target, index, params) ? 0 : 1, params);
}

//==============================================================================
// A button that reports press AND release, not just "clicked".
//
// juce::Button::onStateChange would be the shorter route, but its exact
// firing behaviour isn't something to assume without reading JUCE sources -
// overriding mouseDown/mouseUp is unambiguous, since Component's
// mouse-capture behaviour (mouseUp still reaches the component that started
// the drag, even outside its bounds) is foundational and certain.
//
// Moved here unchanged from MainComponent.h - introduced for item 3's Gate
// button, then reused by the on-screen keyboard, which is why it was worth
// generalising rather than leaving Gate-specific. It has no reason to stay
// private to MainComponent.
//
// NOT final - SynthPanel's PianoKey extends it for the press/release
// semantics only, fully overriding paint() with its own piano-key drawing.
struct MomentaryButton : public juce::TextButton
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

//==============================================================================
#if JUCE_DEBUG

// Debug-only self-test, run once at startup.
//
// Proves ChoiceSpec::firstChoiceValue's offset arithmetic (attachChoice,
// above) against a REAL juce::ComboBox - not a re-implementation of the +/-
// firstChoiceValue maths, same "output is the only proof" discipline every
// other self-test in this codebase follows. Deliberately independent of
// StepDivision: a synthetic 3-entry slice of an imagined 5-entry master
// list, proving the mechanism once so every sliced ChoiceSpec (the Division
// combos, documents/tempo-sync-design.md's follow-up work - whatever else
// slices a shared list tomorrow) inherits the proof rather than needing its
// own. A backwards offset here is a SILENT wrong-value bug for whichever
// atomic a sliced ChoiceSpec targets - exactly the failure class CLAUDE.md
// flags for clock-adjacent DSP work, which is why this exists at all rather
// than trusting the arithmetic by eye.
//
// Scratch VoiceParameters, targeting an arbitrary existing atomic
// (envelopeDestination) purely as somewhere to store into - this test has
// nothing to do with envelope routing itself.
inline void runAttachChoiceOffsetSelfTest()
{
    // Imagine a 5-entry master list "A".."E" (true values 0..4) and a combo
    // that only exposes the 3-entry slice "B","C","D" - true values 1..3 -
    // so firstChoiceValue is 1 (choices[0], "B"'s, own true value).
    static const char* const sliceChoices[] = { "B", "C", "D" };

    const ChoiceSpec spec
    {
        "Test",
        sliceChoices,
        3,
        2,                                      // defaultIndex: "C"'s TRUE value
        &VoiceParameters::envelopeDestination,
        1                                       // firstChoiceValue: "B"'s TRUE value
    };

    VoiceParameters params;
    juce::ComboBox comboBox;
    juce::Label label;

    attachChoice (comboBox, label, spec, params);

    // SEED: must land on the true default (2, "C") - a wrong sign here would
    // seed 2 - 1 = 1 ("B") or 2 + 1 = 3 ("D") instead.
    jassert (params.envelopeDestination.load (std::memory_order_relaxed) == 2);

    // FIRST exposed item ("B", combo-local id 1) must store its true value,
    // firstChoiceValue itself (1) - not 0, which is what a missing offset
    // would store.
    comboBox.setSelectedId (1, juce::dontSendNotification);
    comboBox.onChange();
    jassert (params.envelopeDestination.load (std::memory_order_relaxed) == 1);

    // LAST exposed item ("D", combo-local id 3) must store
    // firstChoiceValue + numChoices - 1 = 1 + 3 - 1 = 3 - not 2, which is
    // what ignoring the offset (storing the combo-local index verbatim)
    // would give.
    comboBox.setSelectedId (3, juce::dontSendNotification);
    comboBox.onChange();
    jassert (params.envelopeDestination.load (std::memory_order_relaxed) == 3);
}

#endif
