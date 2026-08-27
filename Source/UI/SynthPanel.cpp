#include "SynthPanel.h"

#include <cmath>

//==============================================================================
namespace
{
    const char* const envelopeDestinationChoices[] = { "Filter", "Amp", "Both" };
    const char* const lfoWaveformChoices[]         = { "Triangle", "Square", "S & H" };
    const char* const legatoRetriggerChoices[]     = { "Retrigger", "Legato" };
    const char* const notePriorityChoices[]        = { "Last Note", "Highest Note" };

    // Order follows ArpPattern exactly, so the selected index stores straight
    // into the atomic with no mapping - same convention MainComponent.cpp
    // used for its throwaway version of this table.
    const char* const arpPatternChoices[] = { "Up", "Down", "Up-Down", "Random", "As Played" };

    static_assert ((int) (sizeof (arpPatternChoices) / sizeof (arpPatternChoices[0])) == numArpPatterns,
                   "the pattern combo box and ArpPattern must stay in step");

    // Order and text follow StepDivision exactly - longest step first.
    const char* const arpDivisionChoices[] = { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };

    static_assert ((int) (sizeof (arpDivisionChoices) / sizeof (arpDivisionChoices[0])) == numStepDivisions,
                   "the division combo box and StepDivision's table must stay in step");

    // documents/ui-mockup's header CSS: .mark{font-size:19px;font-weight:600}
    // and .tag{font-size:10px} - the real IBM Plex Sans SemiBold/Regular
    // weights, compiled in via CMakeLists.txt's juce_add_binary_data
    // (PanelLookAndFeel::semiBoldTypeface/regularTypeface), not a synthetic
    // .boldened() guess. Sized up from the mockup's 19px so the mark reads
    // as clearly the largest text in the header, next to the 14px status
    // line and 10px tag. One function per size so resized() can measure
    // with the exact same Font it was drawn with.
    juce::Font markFont()
    {
        return juce::Font (juce::FontOptions (24.0f).withTypeface (PanelLookAndFeel::semiBoldTypeface()));
    }

    juce::Font tagFont()
    {
        return juce::Font (juce::FontOptions (18.0f).withTypeface (PanelLookAndFeel::regularTypeface()));
    }

    // Font no longer carries string-width measurement directly in this JUCE
    // version - GlyphArrangement is the stable way to lay out one line and
    // read back its width. numGlyphs = -1 ("to the end") measured close to
    // zero width for a single-glyph run (the accent A clipped to a sliver) -
    // passing the actual glyph count avoids whatever that sentinel does.
    float measuredTextWidth (const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);
        return glyphs.getBoundingBox (0, text.length(), true).getWidth();
    }
}

//==============================================================================
// documents/ui-design.md section 2's ranges table, carried over unchanged
// from the throwaway debugControlSpecs - these were tuned by ear in items
// 2-5 and only the presentation changes here, not the values.
const KnobSpec SynthPanel::vcoKnobSpecs[numVcoKnobs] =
{
    { "Saw",         0.0,  1.0,  0.70, false, 0.0, "", &VoiceParameters::sawLevel   },
    { "Pulse",       0.0,  1.0,  0.00, false, 0.0, "", &VoiceParameters::pulseLevel },
    { "Pulse Width", 0.02, 0.98, 0.50, false, 0.0, "", &VoiceParameters::pulseWidth },
    { "Sub",         0.0,  1.0,  0.00, false, 0.0, "", &VoiceParameters::subLevel   },
    { "Noise",       0.0,  1.0,  0.00, false, 0.0, "", &VoiceParameters::noiseLevel },
};

// Cutoff is the only control that both stores as log2 AND carries a skew -
// skewMidpoint = sqrt(min*max), the same geometric-mean midpoint the
// throwaway scaffolding computed at runtime for its one Hz-valued control.
const KnobSpec SynthPanel::vcfKnobSpecs[numVcfKnobs] =
{
    { "Cutoff",    20.0, 18000.0, 2000.0, true,  600.0, " Hz", &VoiceParameters::cutoffLog2Hz },
    { "Resonance",  0.0,     1.0,   0.20, false,   0.0, "",    &VoiceParameters::resonance    },
    { "Env->Cutoff", 0.0,    8.0,   0.00, false,   0.0, " oct", &VoiceParameters::envToCutoffDepthOctaves },
};

// Attack/Decay/Release share the 0.001-5.0s range, so they share the same
// skew midpoint (sqrt(0.001 * 5.0) ~= 0.0707) - section 2's "same treatment
// Cutoff already has, applied for the same reason".
const KnobSpec SynthPanel::envKnobSpecs[numEnvKnobs] =
{
    { "Attack",  0.001, 5.0, 0.01, false, 0.0707, " s", &VoiceParameters::attackSeconds  },
    { "Decay",   0.001, 5.0, 0.10, false, 0.0707, " s", &VoiceParameters::decaySeconds   },
    { "Sustain", 0.0,   1.0, 0.70, false, 0.0,    "",   &VoiceParameters::sustainLevel   },
    { "Release", 0.001, 5.0, 0.30, false, 0.0707, " s", &VoiceParameters::releaseSeconds },
};

const ChoiceSpec SynthPanel::envChoiceSpecs[numEnvChoices] =
{
    {
        "Destination",
        envelopeDestinationChoices,
        (int) (sizeof (envelopeDestinationChoices) / sizeof (envelopeDestinationChoices[0])),
        (int) EnvelopeDestination::Amp,
        &VoiceParameters::envelopeDestination
    },
};

const KnobSpec SynthPanel::lfoKnobSpecs[numLfoKnobs] =
{
    { "Rate",      0.02, 20.0, 2.0, false, 0.0, " Hz",  &VoiceParameters::lfoRateHz },
    { "->Pitch",   0.0,   1.0, 0.0, false, 0.0, " oct", &VoiceParameters::lfoToPitchDepthOctaves },
    { "->Cutoff",  0.0,   8.0, 0.0, false, 0.0, " oct", &VoiceParameters::lfoToCutoffDepthOctaves },
};

const ChoiceSpec SynthPanel::lfoChoiceSpecs[numLfoChoices] =
{
    {
        "Waveform",
        lfoWaveformChoices,
        (int) (sizeof (lfoWaveformChoices) / sizeof (lfoWaveformChoices[0])),
        (int) Lfo::Waveform::Triangle,
        &VoiceParameters::lfoWaveform
    },
};

// Glide Time's range starts at 0.0, so it can't take a geometric-mean
// midpoint like Cutoff/Attack/Decay/Release do (that would be a skew of
// zero, which setSkewFactorFromMidPoint rejects) - 0.5s/oct is a plausible
// "typically short" glide instead, picked for the same reason: keep the
// short end off the first few pixels of travel.
const KnobSpec SynthPanel::keyboardKnobSpecs[numKeyboardKnobs] =
{
    { "Glide Time", 0.0, 5.0, 0.0, false, 0.5, " s/oct", &VoiceParameters::glideTimeSeconds },
};

const ChoiceSpec SynthPanel::keyboardChoiceSpecs[numKeyboardChoices] =
{
    {
        "Glide Mode",
        legatoRetriggerChoices,
        (int) (sizeof (legatoRetriggerChoices) / sizeof (legatoRetriggerChoices[0])),
        (int) LegatoRetriggerMode::Retrigger,
        &VoiceParameters::legatoRetriggerMode
    },
    {
        "Note Priority",
        notePriorityChoices,
        (int) (sizeof (notePriorityChoices) / sizeof (notePriorityChoices[0])),
        (int) NotePriorityMode::LastNote,
        &VoiceParameters::notePriorityMode
    },
};

// Range matches StepClock's/the arpeggiator's own clamps, so a knob can
// never ask for something the clock or walker will silently refuse - same
// reasoning as the throwaway debugChoiceSpecs table.
const KnobSpec SynthPanel::arpKnobSpecs[numArpKnobs] =
{
    { "Tempo", 20.0, 300.0, 120.0, false, 0.0, " BPM", &VoiceParameters::arpTempoBpm  },
    { "Gate",  0.05,  0.95,  0.50, false, 0.0, "",     &VoiceParameters::arpGateLength },
};

const ChoiceSpec SynthPanel::arpChoiceSpecs[numArpChoices] =
{
    {
        "Pattern",
        arpPatternChoices,
        (int) (sizeof (arpPatternChoices) / sizeof (arpPatternChoices[0])),
        (int) ArpPattern::Up,
        &VoiceParameters::arpPattern
    },
    {
        "Division",
        arpDivisionChoices,
        (int) (sizeof (arpDivisionChoices) / sizeof (arpDivisionChoices[0])),
        (int) StepDivision::Sixteenth,
        &VoiceParameters::arpDivision
    },
};

// Off on first load for both - engaging Hold with nothing held must never
// look armed by surprise, and the instrument must play normally out of the
// box (same reasoning as the throwaway arpEnabledChoices default).
const ToggleSpec SynthPanel::arpToggleSpecs[numArpToggles] =
{
    { "On",   0, &VoiceParameters::arpEnabled },
    { "Hold", 0, &VoiceParameters::arpHold    },
};

const KnobSpec SynthPanel::outputKnobSpecs[numOutputKnobs] =
{
    { "Level", 0.0, 1.0, 0.25, false, 0.0, "", &VoiceParameters::outputLevel },
};

//==============================================================================
// One octave, C3 to C4 inclusive - the closing C makes it read as a keyboard
// rather than stopping awkwardly on B. `letter` matches QwertyNoteInput.cpp's
// keyMap base-row entries exactly (Z S X D C V G B H N J M, then Q for the
// octave above) - same order as semitoneOffset, so this table and that one
// can never quietly disagree about which key plays what.
const SynthPanel::KeyboardKeySpec SynthPanel::keyboardKeySpecs[numKeyboardKeys] =
{
    { "C",   0, false, 'Z' }, { "C#",  1, true,  'S' }, { "D",   2, false, 'X' }, { "D#",  3, true,  'D' },
    { "E",   4, false, 'C' }, { "F",   5, false, 'V' }, { "F#",  6, true,  'G' }, { "G",   7, false, 'B' },
    { "G#",  8, true,  'H' }, { "A",   9, false, 'N' }, { "A#", 10, true,  'J' }, { "B",  11, false, 'M' },
    { "C'", 12, false, 'Q' },
};

//==============================================================================
void SynthPanel::SeqReservedStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (PanelLookAndFeel::sectionFill.withAlpha (0.5f));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (PanelLookAndFeel::outline);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    g.setColour (PanelLookAndFeel::textDim);
    g.setFont (juce::Font (juce::FontOptions (14.0f).withTypeface (PanelLookAndFeel::regularTypeface())));
    g.drawText ("SEQ - reserved for item 7", getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
void SynthPanel::PianoKey::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    auto fill = isBlackKey ? PanelLookAndFeel::keyBlack : PanelLookAndFeel::keyWhite;
    if (isDown())
        fill = fill.darker (0.15f);
    else if (isOver())
        fill = fill.darker (0.05f);

    // Flat top, rounded bottom only - a real piano key's silhouette, not a
    // uniform rounded rect. curveTopLeft/curveTopRight false, the bottom two
    // true.
    juce::Path shape;
    shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                                isBlackKey ? 4.0f : 5.0f, isBlackKey ? 4.0f : 5.0f,
                                false, false, true, true);
    g.setColour (fill);
    g.fillPath (shape);

    if (isBlackKey)
    {
        g.setColour (PanelLookAndFeel::outline);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    // Note name, bottom-anchored - documents/ui-mockup's .wk label spot.
    g.setColour (PanelLookAndFeel::textDim);
    g.setFont (juce::Font (juce::FontOptions (9.0f).withTypeface (PanelLookAndFeel::regularTypeface())));
    g.drawText (noteName, bounds.reduced (2.0f, 4.0f), juce::Justification::centredBottom);
}

//==============================================================================
SynthPanel::PianoKeyboard::PianoKeyboard()
{
    // White keys added first, black keys after - JUCE draws (and hit-tests)
    // later-added children on top of earlier ones, which is what makes a
    // black key actually clickable over the white key underneath it rather
    // than the white key stealing the click.
    for (int i = 0; i < numKeyboardKeys; ++i)
        if (! SynthPanel::keyboardKeySpecs[i].isBlackKey)
            addAndMakeVisible (keys[(size_t) i]);

    for (int i = 0; i < numKeyboardKeys; ++i)
        if (SynthPanel::keyboardKeySpecs[i].isBlackKey)
            addAndMakeVisible (keys[(size_t) i]);

    for (auto& letter : letters)
    {
        letter.setJustificationType (juce::Justification::centred);
        letter.setColour (juce::Label::textColourId, PanelLookAndFeel::textDim);
        letter.setFont (juce::Font (juce::FontOptions (11.0f).withTypeface (PanelLookAndFeel::regularTypeface())));
        addAndMakeVisible (letter);
    }
}

void SynthPanel::PianoKeyboard::paint (juce::Graphics& g)
{
    // The "well" the keys sit in - documents/ui-mockup's .kbd background/
    // border. No separate palette token for that exact tone; reuses the
    // panel's own darkest existing one rather than adding a token for this
    // one spot.
    auto pianoBounds = getLocalBounds().toFloat().withTrimmedBottom ((float) (letterRowHeight + letterGap));
    g.setColour (PanelLookAndFeel::background);
    g.fillRoundedRectangle (pianoBounds, 8.0f);
    g.setColour (PanelLookAndFeel::outline);
    g.drawRoundedRectangle (pianoBounds.reduced (0.5f), 8.0f, 1.0f);
}

void SynthPanel::PianoKeyboard::resized()
{
    // documents/ui-mockup's .kbd/.wk/.bk layout: 8 white keys evenly filling
    // the row (small gap between them), 5 black keys absolutely positioned
    // on top at fixed percentages of the whole keyboard's width - a real
    // octave's black keys are NOT evenly spaced, so this can't be computed
    // from a simple formula the way the white keys can.
    constexpr int padding = 5;
    constexpr int whiteKeyGap = 3;
    constexpr int numWhiteKeys = 8;
    constexpr float blackKeyWidthFraction = 0.074f;    // .bk{width:7.4%}
    constexpr float blackKeyHeightFraction = 0.68f;    // shorter than the white keys, piano-style
    constexpr float blackKeyLeftFractions[5] = { 0.0875f, 0.2125f, 0.4625f, 0.5875f, 0.7125f };

    auto bounds = getLocalBounds();
    auto letterRow = bounds.removeFromBottom (letterRowHeight);
    bounds.removeFromBottom (letterGap);
    // `bounds` now matches documents/ui-mockup's .kbd box exactly (before
    // its own padding is applied) - black key positions are measured
    // against THIS, not the padded area, matching the mockup's own
    // percentages (an absolutely-positioned child's percentages resolve
    // against its containing block's padding box, not a further-reduced
    // content box).

    auto pianoArea = bounds.reduced (padding);
    const auto whiteKeyWidth = (pianoArea.getWidth() - (numWhiteKeys - 1) * whiteKeyGap) / numWhiteKeys;

    std::array<juce::Rectangle<int>, numKeyboardKeys> keyBounds;

    auto x = pianoArea.getX();
    for (int i = 0; i < numKeyboardKeys; ++i)
    {
        if (! SynthPanel::keyboardKeySpecs[i].isBlackKey)
        {
            keyBounds[(size_t) i] = { x, pianoArea.getY(), whiteKeyWidth, pianoArea.getHeight() };
            x += whiteKeyWidth + whiteKeyGap;
        }
    }

    const auto blackKeyWidth = (int) ((float) bounds.getWidth() * blackKeyWidthFraction);
    const auto blackKeyHeight = (int) ((float) pianoArea.getHeight() * blackKeyHeightFraction);
    int blackKeyIndex = 0;

    for (int i = 0; i < numKeyboardKeys; ++i)
    {
        if (SynthPanel::keyboardKeySpecs[i].isBlackKey)
        {
            const auto left = bounds.getX()
                             + (int) ((float) bounds.getWidth() * blackKeyLeftFractions[blackKeyIndex]);
            keyBounds[(size_t) i] = { left, pianoArea.getY(), blackKeyWidth, blackKeyHeight };
            ++blackKeyIndex;
        }
    }

    for (int i = 0; i < numKeyboardKeys; ++i)
    {
        keys[(size_t) i].setBounds (keyBounds[(size_t) i]);
        letters[(size_t) i].setBounds (keyBounds[(size_t) i].getX(), letterRow.getY(),
                                        keyBounds[(size_t) i].getWidth(), letterRow.getHeight());
    }
}

//==============================================================================
SynthPanel::OctaveControl::OctaveControl()
{
    upButton.setWantsKeyboardFocus (false);
    downButton.setWantsKeyboardFocus (false);

    readout.setJustificationType (juce::Justification::centred);
    readout.setColour (juce::Label::textColourId, PanelLookAndFeel::textDim);
    readout.setFont (juce::Font (juce::FontOptions (13.0f).withTypeface (PanelLookAndFeel::regularTypeface())));

    for (auto* shortcut : { &upShortcut, &downShortcut })
    {
        shortcut->setJustificationType (juce::Justification::centred);
        shortcut->setColour (juce::Label::textColourId, PanelLookAndFeel::textDim);
        shortcut->setFont (juce::Font (juce::FontOptions (11.0f).withTypeface (PanelLookAndFeel::regularTypeface())));
        addAndMakeVisible (*shortcut);
    }
    upShortcut.setText (".", juce::dontSendNotification);
    downShortcut.setText (",", juce::dontSendNotification);

    addAndMakeVisible (upButton);
    addAndMakeVisible (readout);
    addAndMakeVisible (downButton);
}

void SynthPanel::OctaveControl::resized()
{
    constexpr int buttonHeight = 26;
    constexpr int shortcutHeight = 14;

    auto area = getLocalBounds();
    upButton.setBounds (area.removeFromTop (buttonHeight));
    upShortcut.setBounds (area.removeFromTop (shortcutHeight));

    downShortcut.setBounds (area.removeFromBottom (shortcutHeight));
    downButton.setBounds (area.removeFromBottom (buttonHeight));

    readout.setBounds (area);
}

//==============================================================================
// AVIJI<em>A</em>TOR - documents/ui-mockup's header markup, verbatim: the
// second A is the only accent-coloured glyph, everything else is the
// ordinary text colour. One AttributedString, not three Labels - see the
// class comment in SynthPanel.h on why that matters for kerning.
void SynthPanel::TitleMark::paint (juce::Graphics& g)
{
    juce::AttributedString str;
    str.append ("AVIJI", markFont(), PanelLookAndFeel::text);
    str.append ("A", markFont(), PanelLookAndFeel::accent);
    str.append ("TOR", markFont(), PanelLookAndFeel::text);
    str.setJustification (juce::Justification::centredLeft);
    str.draw (g, getLocalBounds().toFloat());
}

//==============================================================================
SynthPanel::SynthPanel (VoiceParameters& parametersToControl, std::function<void (const NoteEvent&)> pushNoteEventIn)
    : params (parametersToControl), pushNoteEvent (std::move (pushNoteEventIn))
{
    // Set on THIS component so every child below inherits it by cascade -
    // see the member comment in SynthPanel.h.
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (titleMark);

    // "AUDIO" - corrected from the request's "AUSIO", evidently a typo.
    tagLabel.setText ("NICKOPHONIC AUDIO SYNTHESIS", juce::dontSendNotification);
    tagLabel.setFont (tagFont());
    tagLabel.setColour (juce::Label::textColourId, PanelLookAndFeel::textDim);
    tagLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (tagLabel);

    // One loop shape per spec kind, reused for every section - adding a
    // control to a section is a spec-table row plus one addCell call, never
    // more copy-paste (documents/ui-design.md section 5).
    auto wireKnobs = [this] (auto& cells, const KnobSpec* specs, int count, PanelSection& section)
    {
        for (int i = 0; i < count; ++i)
        {
            auto& cell = cells[(size_t) i];
            attachKnob (cell.slider, cell.label, specs[i], params);
            section.addCell (cell.label, cell.slider);
        }
    };

    auto wireChoices = [this] (auto& cells, const ChoiceSpec* specs, int count, PanelSection& section)
    {
        for (int i = 0; i < count; ++i)
        {
            auto& cell = cells[(size_t) i];
            attachChoice (cell.comboBox, cell.label, specs[i], params);

            // NOT stretched to the knob row's full ~96px height - a combo
            // box that tall reads as a text field, not a dropdown.
            section.addCell (cell.label, cell.comboBox, false);
        }
    };

    wireKnobs (vcoKnobs, vcoKnobSpecs, numVcoKnobs, vcoSection);
    wireKnobs (vcfKnobs, vcfKnobSpecs, numVcfKnobs, vcfSection);

    wireKnobs (envKnobs, envKnobSpecs, numEnvKnobs, envSection);

    // Special-cased rather than routed through wireChoices: this is the one
    // cell that also absorbs envRowWidthCompensation (see the constant's
    // comment in SynthPanel.h) so row A's total width matches row B's.
    for (int i = 0; i < numEnvChoices; ++i)
    {
        auto& cell = envChoices[(size_t) i];
        attachChoice (cell.comboBox, cell.label, envChoiceSpecs[i], params);
        envSection.addCell (cell.label, cell.comboBox, false, envRowWidthCompensation);
    }

    wireKnobs (lfoKnobs, lfoKnobSpecs, numLfoKnobs, lfoSection);
    wireChoices (lfoChoices, lfoChoiceSpecs, numLfoChoices, lfoSection);

    wireKnobs (keyboardKnobs, keyboardKnobSpecs, numKeyboardKnobs, keyboardSection);
    wireChoices (keyboardChoices, keyboardChoiceSpecs, numKeyboardChoices, keyboardSection);

    // ARP's cell order is On+Hold, Pattern, Division, Tempo, Gate
    // (documents/ui-design.md section 2) - the toggle-stack cell is added
    // FIRST, before the choices/knobs below, so PanelSection's cells land in
    // that same order.
    attachToggle (arpToggleStack.top, arpToggleSpecs[0], params);
    attachToggle (arpToggleStack.bottom, arpToggleSpecs[1], params);
    arpSection.addCell (arpToggleCaption, arpToggleStack);

    wireChoices (arpChoices, arpChoiceSpecs, numArpChoices, arpSection);
    wireKnobs (arpKnobs, arpKnobSpecs, numArpKnobs, arpSection);

    wireKnobs (outputKnobs, outputKnobSpecs, numOutputKnobs, outputSection);

    for (auto* section : { &vcoSection, &vcfSection, &envSection, &lfoSection,
                            &keyboardSection, &arpSection, &outputSection })
        addAndMakeVisible (*section);

    addAndMakeVisible (seqStrip);

    //==========================================================================
    // On-screen piano keyboard - see PianoKey/PianoKeyboard in the header for
    // why this is hand-painted rather than TextButton-styled or built from
    // juce::MidiKeyboardComponent (a large unverified surface not worth
    // bridging into the FIFO).
    for (int i = 0; i < numKeyboardKeys; ++i)
    {
        const auto& spec = keyboardKeySpecs[i];
        auto& key = pianoKeyboard.keys[(size_t) i];

        key.setButtonText (spec.name);
        key.setWantsKeyboardFocus (false);
        key.isBlackKey = spec.isBlackKey;
        key.noteName = spec.name;

        key.onPressedChanged = [this, &spec, &key] (bool isDown)
        {
            // Captured on press, replayed on release - see the
            // emittedNoteNumber comment on PianoKey in the header.
            if (isDown)
                key.emittedNoteNumber = (std::uint8_t) juce::jlimit (0, 127,
                    keyboardBaseNoteNumber + octaveShift * 12 + spec.semitoneOffset);

            pushNoteEvent ({ isDown ? NoteEvent::Type::NoteOn : NoteEvent::Type::NoteOff,
                              key.emittedNoteNumber,
                              pitchLog2HzForMidiNote (key.emittedNoteNumber),
                              1.0f });
        };

        pianoKeyboard.letters[(size_t) i].setText (juce::String::charToString ((juce::juce_wchar) spec.letter),
                                                     juce::dontSendNotification);
    }

    addAndMakeVisible (pianoKeyboard);

    octaveControl.upButton.onClick = [this]
    {
        if (onOctaveUpClicked != nullptr)
            onOctaveUpClicked();
    };
    octaveControl.downButton.onClick = [this]
    {
        if (onOctaveDownClicked != nullptr)
            onOctaveDownClicked();
    };
    addAndMakeVisible (octaveControl);
    setOctaveShift (0); // matches QwertyNoteInput's own starting shift

    audioSettingsButton.setButtonText ("Audio Settings");
    audioSettingsButton.setWantsKeyboardFocus (false);
    audioSettingsButton.onClick = [this]
    {
        if (onAudioSettingsClicked != nullptr)
            onAudioSettingsClicked();
    };
    addAndMakeVisible (audioSettingsButton);

    // The fixed design size, per section 3 - NOT the real window size. Step
    // 6's scale transform maps this onto whatever the actual window is.
    setSize (designWidth, designHeight);
}

void SynthPanel::setOctaveShift (int newShift)
{
    octaveShift = newShift;

    // keyboardBaseNoteNumber (MIDI 48) is C3 - see that constant's comment -
    // so the readout is just "3 + shift".
    octaveControl.readout.setText ("C" + juce::String (3 + octaveShift), juce::dontSendNotification);
}

SynthPanel::~SynthPanel()
{
    // Must happen before `lookAndFeel` is destroyed, or JUCE asserts on
    // shutdown - documents/ui-design.md section 7.
    setLookAndFeel (nullptr);
}

//==============================================================================
void SynthPanel::paint (juce::Graphics& g)
{
    g.fillAll (PanelLookAndFeel::panel);
}

void SynthPanel::resized()
{
    // All of section 3's vertical/horizontal budget, as constants - computed
    // against designWidth/designHeight only, per the class comment in the
    // header. Never getWidth()/getHeight(): this component's bounds may
    // transiently differ (e.g. before step 6's transform is applied), and
    // the layout must not depend on that.
    constexpr int sideMargin = 20;
    constexpr int topMargin = 16;
    constexpr int bottomMargin = 16;
    constexpr int gap = 12;
    constexpr int sectionGap = 16;
    constexpr int headerHeight = 36;
    constexpr int audioSettingsWidth = 130;
    constexpr int audioSettingsHeight = 28;
    constexpr int seqStripHeight = 90;
    constexpr int keyboardRowHeight = 84 + PianoKeyboard::letterGap + PianoKeyboard::letterRowHeight;

    auto area = juce::Rectangle<int> (0, 0, designWidth, designHeight);
    area.removeFromTop (topMargin);
    area.removeFromBottom (bottomMargin);
    area = area.withTrimmedLeft (sideMargin).withTrimmedRight (sideMargin);

    // Mark + tagline left, Audio Settings top-right - one row, so Audio
    // Settings reads as a global control rather than being buried at the
    // bottom. Whatever's left between them is unused now that the
    // play-instructions status text is gone.
    {
        auto headerRow = area.removeFromTop (headerHeight);

        audioSettingsButton.setBounds (headerRow.removeFromRight (audioSettingsWidth)
                                                 .withSizeKeepingCentre (audioSettingsWidth, audioSettingsHeight));

        // Measured, not guessed - +2px is anti-aliasing slack, not a
        // workaround (measuredTextWidth's numGlyphs fix above resolved the
        // actual under-measurement bug).
        const auto markWidth = (int) std::ceil (measuredTextWidth (markFont(), "AVIJIATOR")) + 2;

        constexpr int markTagGap = 14; // documents/ui-mockup's .hdr gap

        titleMark.setBounds (headerRow.removeFromLeft (markWidth));
        headerRow.removeFromLeft (markTagGap);

        // Claims the rest of the header row's width - now that the
        // play-instructions status text is gone, that's ample room, and
        // tagLabel's own centredLeft justification (constructor) keeps the
        // text flush against the mark rather than centred across it.
        //
        // Height still clamped to one text line, not the full headerHeight:
        // a Label's default LookAndFeel wraps onto as many lines as its
        // BOUNDS HEIGHT divides into by the font's line height
        // (drawFittedText's numLines argument), so handing it the full 36px
        // header height let "NICKOPHONIC AUDIO SYNTHESIS" wrap across two
        // lines. The earlier attempt fixed that by also shrinking the WIDTH
        // down to the measured text width - too tight to render at full
        // size, so drawFittedText then squeezed the font horizontally and
        // ellipsised it to fit. Generous width plus one-line height avoids
        // both failure modes at once.
        const auto tagLineHeight = (int) std::ceil (tagFont().getHeight());
        tagLabel.setBounds (headerRow.withSizeKeepingCentre (headerRow.getWidth(), tagLineHeight));
    }
    area.removeFromTop (gap);

    // Places one section, left-to-right, leaving `sectionGap` after it -
    // shared by both section rows below. extraWidth matches the total extra
    // column width spent on that section's own addCell calls (see ENV
    // below).
    const auto place = [sectionGap] (juce::Rectangle<int>& row, PanelSection& section, int numCells, int extraWidth = 0)
    {
        section.setBounds (row.removeFromLeft (PanelSection::widthForCells (numCells, extraWidth)));
        row.removeFromLeft (sectionGap);
    };

    // Section row A: VCO | VCF | ENV. ENV gets envRowWidthCompensation so
    // this row's total width matches row B's - see that constant's comment
    // in SynthPanel.h.
    {
        auto row = area.removeFromTop (PanelSection::heightForCells());
        place (row, vcoSection, numVcoKnobs);
        place (row, vcfSection, numVcfKnobs);
        place (row, envSection, numEnvKnobs + numEnvChoices, envRowWidthCompensation);
    }
    area.removeFromTop (gap);

    // Section row B: LFO | KEYBOARD | ARP | OUTPUT. These four sections'
    // widthForCells sum plus three sectionGaps fill the row's 1240px exactly
    // - see documents/ui-design.md section 3's horizontal budget.
    {
        auto row = area.removeFromTop (PanelSection::heightForCells());
        place (row, lfoSection, numLfoKnobs + numLfoChoices);
        place (row, keyboardSection, numKeyboardKnobs + numKeyboardChoices);
        place (row, arpSection, 1 + numArpChoices + numArpKnobs); // 1 = the On/Hold stacked cell
        place (row, outputSection, numOutputKnobs);
    }
    area.removeFromTop (gap);

    seqStrip.setBounds (area.removeFromTop (seqStripHeight));
    area.removeFromTop (gap);

    // PianoKeyboard lays out its own keys and letter row internally - see
    // PianoKeyboard::resized(). Audio Settings lives in the header, top-right
    // - see above; the latching note buttons are gone (removed entirely).
    // OctaveControl sits to the left of it, same row height, its own width
    // carved off before the keyboard claims the rest.
    {
        constexpr int octaveControlWidth = 64;

        auto keyboardRow = area.removeFromTop (keyboardRowHeight);
        octaveControl.setBounds (keyboardRow.removeFromLeft (octaveControlWidth));
        keyboardRow.removeFromLeft (gap);
        pianoKeyboard.setBounds (keyboardRow);
    }
}
