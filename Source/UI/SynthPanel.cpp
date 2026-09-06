#include "SynthPanel.h"

#include <cmath>

//==============================================================================
namespace
{
    const char* const envelopeDestinationChoices[] = { "Filter", "Amp", "Both" };
    // Order follows Lfo::Waveform exactly, same convention as arpPatternChoices
    // below - the enum's own declaration order was changed to match this
    // display order (not the other way around), since every reference to a
    // specific waveform elsewhere is by name, not by underlying int.
    const char* const lfoWaveformChoices[] = { "Ramp", "Triangle", "Sine", "Square", "S & H" };
    const char* const legatoRetriggerChoices[]     = { "Retrigger", "Legato" };
    const char* const notePriorityChoices[]        = { "Last Note", "Highest Note" };

    // Order follows ArpPattern exactly, so the selected index stores straight
    // into the atomic with no mapping - same convention MainComponent.cpp
    // used for its throwaway version of this table.
    const char* const arpPatternChoices[] = { "Up", "Down", "Up-Down", "Random", "As Played" };

    static_assert ((int) (sizeof (arpPatternChoices) / sizeof (arpPatternChoices[0])) == numArpPatterns,
                   "the pattern combo box and ArpPattern must stay in step");

    // Order and text follow StepDivision exactly - longest step first.
    // Reused for the arp's Division combo, item 7 build step 6's sequencer
    // one, and tempo-sync-design.md's LFO Sync Division combo - StepDivision
    // is one shared enum, and ChoiceSpec's `choices` is just a pointer, so
    // the same table backs several ChoiceSpecs with different targets.
    // Grown twice at the user's request: 1/1 and 1/2 first, then the five
    // multi-bar entries (32/1..2/1) on top of that for the LFO specifically
    // ("slow evolutions of sound") - StepClock.h's own comment on
    // StepDivision has the full reasoning. Arp/seq's own Division
    // ChoiceSpecs (arpChoiceSpecs/seqChoiceSpecs below) deliberately slice
    // OFF the five multi-bar entries via ChoiceSpec::firstChoiceValue - a
    // single arp/seq step lasting more than a bar is tedious rather than
    // musical, unlike an LFO sweeping that slowly.
    const char* const arpDivisionChoices[] =
    {
        "32/1", "16/1", "8/1", "4/1", "2/1",             // LFO Sync Division only
        "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"
    };

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
        return PanelLookAndFeel::fontFor (PanelLookAndFeel::semiBoldTypeface(), 24.0f);
    }

    juce::Font tagFont()
    {
        return PanelLookAndFeel::fontFor (PanelLookAndFeel::regularTypeface(), 18.0f);
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

    //==========================================================================
    // Item 7 build step 6: StepCell's pitch-lane readout and drag math both
    // need the inverse of NoteEvent.h's pitchLog2HzForMidiNote - that
    // function only goes note-number -> pitch, never back.
    int midiNoteForPitchLog2Hz (float pitchLog2Hz) noexcept
    {
        // Same anchor as pitchLog2HzForMidiNote: log2(440) = 8.78136,
        // MIDI 69 = A4 by definition.
        return (int) std::lround ((double) (pitchLog2Hz - 8.78136f) * 12.0) + 69;
    }

    // StepCell::mouseDrag's pitch range, in this project's octave-naming
    // convention (noteNameForMidiNote below anchors MIDI 48 as "C3", so C1 is
    // two octaves down and C8 five octaves up). Housekeeping fix
    // (documents/TODO.md): an unbounded drag could previously push a step's
    // pitch arbitrarily far in either direction.
    constexpr int minStepMidiNote = 24;  // C1
    constexpr int maxStepMidiNote = 108; // C8

    // "C3" for MIDI 48, matching SynthPanel::keyboardBaseNoteNumber's own
    // octave anchor exactly (the OUTPUT panel's Octave knob uses the
    // identical convention - see its textFromValueFunction) - duplicated as
    // a literal rather than reaching into that private constant.
    juce::String noteNameForMidiNote (int midiNoteNumber) noexcept
    {
        static const char* const names[12] =
        {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };

        constexpr int midiNoteForC3 = 48;
        const auto semitoneFromC3 = midiNoteNumber - midiNoteForC3;

        // Floor division, not truncation - a negative semitoneFromC3 (any
        // note below C3) must still land in the right octave rather than
        // rounding toward zero.
        const auto octave = 3 + (int) std::floor ((float) semitoneFromC3 / 12.0f);
        const auto nameIndex = ((semitoneFromC3 % 12) + 12) % 12;

        return juce::String (names[nameIndex]) + juce::String (octave);
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

// Rate's floor widened 0.02 -> 0.005 Hz (~200s cycle), documents/
// tempo-sync-design.md section 1 - free-run mode's own "should be able to
// sweep even slower" ask, deliberately independent of the sync ratios below.
// Lfo::setRate has no internal clamp, so this knob range is the only
// enforcement - and the wider low end needs a skew, same geometric-mean
// convention as Cutoff/Attack/Decay/Release above, or it would be crammed
// into the first few pixels of travel.
const KnobSpec SynthPanel::lfoKnobSpecs[numLfoKnobs] =
{
    { "Rate",      0.005, 20.0, 2.0, false, 0.3162, " Hz",  &VoiceParameters::lfoRateHz },
    { "->Pitch",   0.0,   1.0, 0.0, false, 0.0,    " oct", &VoiceParameters::lfoToPitchDepthOctaves },
    { "->Cutoff",  0.0,   8.0, 0.0, false, 0.0,    " oct", &VoiceParameters::lfoToCutoffDepthOctaves },
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
    // Reuses arpDivisionChoices - documents/tempo-sync-design.md section 4: a
    // third user of that one shared table, same "one table, several
    // ChoiceSpecs with different targets" precedent the arp/seq split above
    // already established. Unlike arp/seq's own Division ChoiceSpecs, this
    // one is NOT sliced (no firstChoiceValue) - the LFO is the one consumer
    // that sees the full range, multi-bar entries included.
    {
        "Sync Division",
        arpDivisionChoices,
        (int) (sizeof (arpDivisionChoices) / sizeof (arpDivisionChoices[0])),
        (int) StepDivision::Sixteenth,
        &VoiceParameters::lfoSyncDivision
    },
};

// LFO's first-ever toggle - see lfoSyncToggle's own comment in SynthPanel.h.
const ToggleSpec SynthPanel::lfoToggleSpecs[numLfoToggles] =
{
    { "Sync", 0, &VoiceParameters::lfoSyncEnabled },
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

// Range matches the arp's own gate-fraction clamp, so the knob can never ask
// for something the walker will silently refuse - same reasoning as the
// throwaway debugChoiceSpecs table. Tempo used to live here too (Tempo,
// Gate) - moved to OUTPUT, next to Level, once masterTempoBpm was reading
// live: see outputKnobSpecs' own comment below.
const KnobSpec SynthPanel::arpKnobSpecs[numArpKnobs] =
{
    { "Gate", 0.05, 0.95, 0.50, false, 0.0, "", &VoiceParameters::arpGateLength },
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
        // Sliced to Whole..ThirtySecond - see arpDivisionChoices' own
        // comment above for why the five multi-bar entries are LFO-only.
        arpDivisionChoices + (int) StepDivision::Whole,
        numStepDivisions - (int) StepDivision::Whole,
        (int) StepDivision::Sixteenth,   // TRUE global default - ChoiceSpec's own comment
        &VoiceParameters::arpDivision,
        (int) StepDivision::Whole        // firstChoiceValue
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

// Tempo moved here from ARP (arpKnobSpecs' own comment above) - reconsidered
// after tempo-sync-design.md's build: masterTempoBpm reads globally now (arp,
// sequencer and the synced LFO all follow it), so a control panel position
// implying it's arp-owned was misleading. OUTPUT, next to Level, has no such
// implication - a plain global-controls cluster.
const KnobSpec SynthPanel::outputKnobSpecs[numOutputKnobs] =
{
    { "Level", 0.0,   1.0, 0.25, false, 0.0, "",    &VoiceParameters::outputLevel  },
    { "Tempo", 20.0, 300.0, 120.0, false, 0.0, " BPM", &VoiceParameters::masterTempoBpm },
};

// Item 7 build step 6. Gate range mirrors the arp's own knob exactly
// (arpKnobSpecs above) - same StepClock-backed clamps, same reasoning: a
// knob must never be able to ask for something the clock or the sequencer's
// own gate-fraction clamp will silently refuse. Tempo knob removed -
// documents/tempo-sync-design.md: the sequencer now reads the shared
// masterTempoBpm dial, which lives in OUTPUT (outputKnobSpecs above).
const KnobSpec SynthPanel::seqKnobSpecs[numSeqKnobs] =
{
    { "Gate",  0.05,  0.95,  0.50, false, 0.0, "",     &VoiceParameters::seqGateLength },
};

const ChoiceSpec SynthPanel::seqChoiceSpecs[numSeqChoices] =
{
    {
        "Division",
        // Sliced the same way as arpChoiceSpecs' own Division entry - see
        // arpDivisionChoices' own comment above.
        arpDivisionChoices + (int) StepDivision::Whole,
        numStepDivisions - (int) StepDivision::Whole,
        (int) StepDivision::Sixteenth,   // TRUE global default
        &VoiceParameters::seqDivision,
        (int) StepDivision::Whole        // firstChoiceValue
    },
};

// Off on first load, same reasoning as arpToggleSpecs' "On" entry - the
// instrument must not start sequencing notes nobody programmed (or, for
// Record, start capturing over a pattern nobody armed it to).
const ToggleSpec SynthPanel::seqToggleSpecs[numSeqToggles] =
{
    { "On",     0, &VoiceParameters::seqEnabled     },
    { "Record", 0, &VoiceParameters::seqRecordArmed },
};

//==============================================================================
// Item 10 (documents/character-and-vim.md). Both knobs are 0..1, 0-default -
// the same "0 = no effect" convention every other depth knob in this file
// already uses (envToCutoffDepthOctaves, lfoToPitchDepthOctaves, ...), not a
// new pattern. Drive is knob-only, no separate toggle - the doc's own
// scope-cut note for why. Drive's target REVISED from filterDriveAmount to
// driveAmount when A1 moved from an in-filter-feedback-loop stage to a
// pre-filter drive/distortion one (Source/DSP/Drive.h) - the knob itself,
// its cell, and its display name are all unchanged.
const KnobSpec SynthPanel::characterKnobSpecs[numCharacterKnobs] =
{
    { "Drive",     0.0, 1.0, 0.0, false, 0.0, "", &VoiceParameters::driveAmount    },
    { "Humanise",  0.0, 1.0, 0.0, false, 0.0, "", &VoiceParameters::humaniseAmount },
};

// Off on first load, same reasoning as every other "instrument must play
// normally out of the box" toggle in this file (arpToggleSpecs' "On" entry,
// seqToggleSpecs' "On" entry) - a clean/clinical voice is the safe default,
// per the doc's own "always tasteful, but stay reachable" design principle.
const ToggleSpec SynthPanel::characterToggleSpecs[numCharacterToggles] =
{
    { "Vim",    0, &VoiceParameters::vimEnabled    },
    { "Chorus", 0, &VoiceParameters::chorusEnabled },
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
void SynthPanel::StepCell::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    const auto gateOn = loadStepFlag (&VoiceParameters::stepGateOn, index, *parameters);
    const auto accent = loadStepFlag (&VoiceParameters::stepAccent, index, *parameters);
    const auto slide  = loadStepFlag (&VoiceParameters::stepSlide,  index, *parameters);

    // Beat grouping - alternating shading per 4-step group, the same visual
    // convention a hardware step sequencer's grid uses so a 16-cell row
    // reads at a glance rather than needing to be counted.
    const auto groupIsAlt = (index / 4) % 2 == 1;
    auto background = groupIsAlt ? PanelLookAndFeel::sectionFill.brighter (0.04f)
                                  : PanelLookAndFeel::sectionFill;

    if (gateOn)
        background = accent ? PanelLookAndFeel::accent : PanelLookAndFeel::accent.withAlpha (0.55f);

    g.setColour (background);
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (isCurrentlyPlaying ? PanelLookAndFeel::accentAlt : PanelLookAndFeel::outline);
    g.drawRoundedRectangle (bounds, 3.0f, isCurrentlyPlaying ? 2.0f : 1.0f);

    // The currently selected lane's value, overlaid on top of the gate/
    // accent state above rather than replacing it - see the class comment
    // for why pitch reads as text but cutoff/resonance read as a fill bar.
    const auto lane = selectedLane != nullptr ? *selectedLane : 0;

    if (lane == 0)
    {
        const auto pitchLog2Hz = loadStepValue (&VoiceParameters::stepPitchLog2Hz, index, *parameters);

        g.setColour (gateOn ? PanelLookAndFeel::text : PanelLookAndFeel::textDim);
        g.setFont (PanelLookAndFeel::fontFor (PanelLookAndFeel::regularTypeface(), 11.0f));
        g.drawText (noteNameForMidiNote (midiNoteForPitchLog2Hz (pitchLog2Hz)),
                    bounds.toNearestInt(), juce::Justification::centred);
    }
    else
    {
        const auto value = loadStepValue (lane == 1 ? &VoiceParameters::stepCutoffNorm
                                                      : &VoiceParameters::stepResonanceNorm,
                                            index, *parameters);

        auto barBounds = bounds.reduced (bounds.getWidth() * 0.3f, 0.0f)
                                .withTrimmedTop (bounds.getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, value)));
        g.setColour (PanelLookAndFeel::accentAlt.withAlpha (0.85f));
        g.fillRect (barBounds);
    }

    // Slide - a small triangle pointing at the next cell, drawn last so it
    // sits on top of everything else.
    if (slide)
    {
        g.setColour (PanelLookAndFeel::text);
        const auto right = bounds.getRight();
        const auto midY = bounds.getCentreY();
        juce::Path arrow;
        arrow.addTriangle (right - 8.0f, midY - 4.0f, right - 8.0f, midY + 4.0f, right - 2.0f, midY);
        g.fillPath (arrow);
    }
}

void SynthPanel::StepCell::mouseDown (const juce::MouseEvent& e)
{
    isDraggedFar = false;
    pendingRightClick = e.mods.isRightButtonDown();
    pendingShiftClick = e.mods.isShiftDown();
    dragStartY = e.getPosition().y;

    const auto lane = selectedLane != nullptr ? *selectedLane : 0;
    dragStartValue = lane == 0 ? loadStepValue (&VoiceParameters::stepPitchLog2Hz, index, *parameters)
                    : lane == 1 ? loadStepValue (&VoiceParameters::stepCutoffNorm, index, *parameters)
                                : loadStepValue (&VoiceParameters::stepResonanceNorm, index, *parameters);
}

void SynthPanel::StepCell::mouseDrag (const juce::MouseEvent& e)
{
    if ((float) e.getDistanceFromDragStart() < dragThreshold)
        return;

    isDraggedFar = true;

    const auto lane = selectedLane != nullptr ? *selectedLane : 0;

    // UP is positive - JUCE's Y grows downward, so a smaller current Y than
    // the drag's start means the mouse moved up, which is "increase" for
    // both a pitch (higher note) and a lane value (more cutoff/resonance).
    const auto deltaY = (float) (dragStartY - e.getPosition().y);

    if (lane == 0)
    {
        constexpr float pixelsPerSemitone = 8.0f;
        const auto semitoneDelta = (int) std::round (deltaY / pixelsPerSemitone);
        const auto startMidiNote = midiNoteForPitchLog2Hz (dragStartValue);
        const auto newMidiNote = juce::jlimit (minStepMidiNote, maxStepMidiNote,
                                                startMidiNote + semitoneDelta);
        storeStepValue (&VoiceParameters::stepPitchLog2Hz, index,
                         pitchLog2HzForMidiNote (newMidiNote), *parameters);
    }
    else
    {
        const auto range = (float) juce::jmax (1, getHeight());
        const auto newValue = juce::jlimit (0.0f, 1.0f, dragStartValue + deltaY / range);
        storeStepValue (lane == 1 ? &VoiceParameters::stepCutoffNorm : &VoiceParameters::stepResonanceNorm,
                         index, newValue, *parameters);
    }

    repaint();
}

void SynthPanel::StepCell::mouseUp (const juce::MouseEvent&)
{
    // A real drag already applied its change live in mouseDrag above - a
    // click gesture (gate/accent/slide) only fires when the gesture turned
    // out NOT to be a drag, so the two never both fire for the same press.
    if (isDraggedFar)
        return;

    if (pendingRightClick)
        toggleStepFlag (&VoiceParameters::stepAccent, index, *parameters);
    else if (pendingShiftClick)
        toggleStepFlag (&VoiceParameters::stepSlide, index, *parameters);
    else
        toggleStepFlag (&VoiceParameters::stepGateOn, index, *parameters);

    repaint();
}

//==============================================================================
SynthPanel::StepGrid::StepGrid()
{
    for (auto& cell : cells)
        addAndMakeVisible (cell);
}

SynthPanel::StepGrid::~StepGrid()
{
    stopTimer();
}

void SynthPanel::StepGrid::configure (VoiceParameters& parametersToControl) noexcept
{
    parameters = &parametersToControl;

    for (int i = 0; i < seqMaxSteps; ++i)
    {
        auto& cell = cells[(size_t) i];
        cell.parameters = parameters;
        cell.index = i;
        cell.selectedLane = &selectedLane;
    }

    // Comfortably above a 16th note at any playable tempo - see the
    // constant's own comment in the header for why this polls rather than
    // each cell reading currentStepForUi directly in its own paint().
    startTimerHz (highlightHz);
}

void SynthPanel::StepGrid::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (PanelLookAndFeel::sectionFill);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (PanelLookAndFeel::outline);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}

void SynthPanel::StepGrid::resized()
{
    constexpr int edgePadding = 6;
    auto bounds = getLocalBounds().reduced (edgePadding);
    const auto cellWidth = bounds.getWidth() / seqMaxSteps;

    for (int i = 0; i < seqMaxSteps; ++i)
    {
        // The last cell absorbs any leftover pixels from the integer
        // division above, same reasoning as every other fixed-width layout
        // in this file that doesn't divide evenly.
        const auto width = (i == seqMaxSteps - 1) ? bounds.getWidth() - cellWidth * (seqMaxSteps - 1)
                                                    : cellWidth;
        cells[(size_t) i].setBounds (bounds.getX() + cellWidth * i, bounds.getY(), width, bounds.getHeight());
    }
}

void SynthPanel::StepGrid::timerCallback()
{
    if (parameters == nullptr)
        return;

    const auto currentStep = parameters->currentStepForUi.load (std::memory_order_relaxed);
    if (currentStep == lastHighlightedStep)
        return;

    if (lastHighlightedStep >= 0 && lastHighlightedStep < seqMaxSteps)
    {
        cells[(size_t) lastHighlightedStep].isCurrentlyPlaying = false;
        cells[(size_t) lastHighlightedStep].repaint();
    }

    if (currentStep >= 0 && currentStep < seqMaxSteps)
    {
        cells[(size_t) currentStep].isCurrentlyPlaying = true;
        cells[(size_t) currentStep].repaint();
    }

    lastHighlightedStep = currentStep;
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
    g.setFont (PanelLookAndFeel::fontFor (PanelLookAndFeel::regularTypeface(), 9.0f));
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
        letter.setFont (PanelLookAndFeel::fontFor (PanelLookAndFeel::regularTypeface(), 11.0f));
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
// AVIJI<em>A</em>TOR - documents/ui-mockup's header markup, verbatim: the
// second A is the only accent-coloured glyph, everything else is the
// ordinary text colour. One AttributedString, not three Labels - see the
// class comment in SynthPanel.h on why that matters for kerning.
void SynthPanel::TitleMark::paint (juce::Graphics& g)
{
    juce::AttributedString str;
    str.append ("AVIJI", markFont(), PanelLookAndFeel::text);
    str.append ("A", markFont(), PanelLookAndFeel::accentAlt);
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

    // Sync toggle cell first - same "toggle cell goes first" precedent as
    // ARP/SEQUENCER below.
    attachToggle (lfoSyncToggle, lfoToggleSpecs[0], params);
    lfoSection.addCell (lfoSyncCaption, lfoSyncToggle, false);

    wireKnobs (lfoKnobs, lfoKnobSpecs, numLfoKnobs, lfoSection);
    wireChoices (lfoChoices, lfoChoiceSpecs, numLfoChoices, lfoSection);

    wireKnobs (keyboardKnobs, keyboardKnobSpecs, numKeyboardKnobs, keyboardSection);
    wireChoices (keyboardChoices, keyboardChoiceSpecs, numKeyboardChoices, keyboardSection);

    // ARP's cell order is On+Hold, Pattern, Division, Gate - documents/
    // ui-design.md section 2's original order minus Tempo, moved to OUTPUT
    // (outputKnobSpecs' own comment) - the toggle-stack cell is added FIRST,
    // before the choices/knobs below, so PanelSection's cells land in that
    // same order.
    attachToggle (arpToggleStack.top, arpToggleSpecs[0], params);
    attachToggle (arpToggleStack.bottom, arpToggleSpecs[1], params);
    arpSection.addCell (arpToggleCaption, arpToggleStack);

    wireChoices (arpChoices, arpChoiceSpecs, numArpChoices, arpSection);
    wireKnobs (arpKnobs, arpKnobSpecs, numArpKnobs, arpSection);

    // outputKnobSpecs index 1 is Tempo - attached directly rather than
    // through wireKnobs, since Octave's hand-wiring below needs to land
    // between Tempo and Level.
    attachKnob (outputKnobs[1].slider, outputKnobs[1].label, outputKnobSpecs[1], params);
    outputSection.addCell (outputKnobs[1].label, outputKnobs[1].slider);

    // Octave joins Level/Tempo as OUTPUT's third global control - see
    // outputSection's own comment in SynthPanel.h. Not a plain wireKnobs/
    // KnobSpec cell like its neighbours: masterOctaveShift is an atomic<int>,
    // stepped over 7 whole-octave positions with a signed-integer readout
    // rather than a numeric suffix, neither of which KnobSpec's
    // float-continuous contract covers - hand-wired instead, same precedent
    // as seqPatternLengthCombo/seqLaneCombo below.
    octaveKnob.label.setText ("OCTAVE", juce::dontSendNotification);
    octaveKnob.label.setJustificationType (juce::Justification::centred);
    octaveKnob.label.setFont (PanelLookAndFeel::captionFont());

    octaveKnob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

    // Read-only text box: there's no sane inverse for "C3" back to a value,
    // so typing is simply not offered - drag-only, same as any other
    // detented hardware control. Otherwise identical geometry to attachKnob's
    // own text box.
    octaveKnob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 70, 16);

    // Interval 1.0 is what makes the drag DETENTED - it snaps to whole
    // octaves rather than gliding continuously, matching a real hardware
    // octave switch's feel despite RotaryHorizontalVerticalDrag being the
    // same continuous-drag style every other knob here uses.
    octaveKnob.slider.setRange (VoiceParameters::minMasterOctaveShift,
                                 VoiceParameters::maxMasterOctaveShift, 1.0);
    octaveKnob.slider.setWantsKeyboardFocus (false);

    // Signed integer, not "C3"-style absolute note naming - for a TRANSPOSE
    // control, "+1"/"-2" reads as "how far from normal" at a glance, which is
    // the thing actually being dialled in; a note name makes the player do
    // that subtraction themselves.
    octaveKnob.slider.textFromValueFunction = [] (double value)
    {
        const auto shift = (int) std::round (value);
        return shift > 0 ? "+" + juce::String (shift) : juce::String (shift);
    };

    octaveKnob.slider.onValueChange = [this]
    {
        params.masterOctaveShift.store ((int) std::round (octaveKnob.slider.getValue()),
                                         std::memory_order_relaxed);
    };

    refreshOctaveReadout(); // seed: the knob and the atomic cannot disagree at startup

    // setValue(0, ...) above is a no-op when the slider's own starting value
    // is ALREADY 0 - juce::Slider skips updateText() when the value doesn't
    // change, so the text box would otherwise be left showing its raw
    // pre-textFromValueFunction default rather than "0", not because the
    // formatting is wrong but because it was never asked to run. Forced
    // unconditionally rather than relying on refreshOctaveReadout to always
    // change something.
    octaveKnob.slider.updateText();

    outputSection.addCell (octaveKnob.label, octaveKnob.slider);

    // outputKnobSpecs index 0 is Level.
    attachKnob (outputKnobs[0].slider, outputKnobs[0].label, outputKnobSpecs[0], params);
    outputSection.addCell (outputKnobs[0].label, outputKnobs[0].slider);

    // Item 7 build steps 6-7. Cell order: On+Record, Division, Pattern
    // Length, Tempo, Gate, Lane - see the member comment in SynthPanel.h.
    // The toggle-stack cell is added FIRST, same "toggle cell goes first"
    // precedent as ARP's above.
    attachToggle (seqToggleStack.top, seqToggleSpecs[0], params);
    attachToggle (seqToggleStack.bottom, seqToggleSpecs[1], params);
    seqControlSection.addCell (seqToggleCaption, seqToggleStack);

    wireChoices (seqChoices, seqChoiceSpecs, numSeqChoices, seqControlSection);

    // Pattern Length: NOT attachChoice - see numSeqChoices' own comment in
    // the header on why a plain 1..16 count can't share attachChoice's
    // index-is-the-value contract the way every enum-backed ChoiceSpec does.
    // Item IDs are set to the length itself (1..16), so getSelectedId() can
    // be stored straight into the atomic with no off-by-one translation.
    seqPatternLengthLabel.setText ("PATTERN LENGTH", juce::dontSendNotification);
    seqPatternLengthLabel.setJustificationType (juce::Justification::centred);
    seqPatternLengthLabel.setFont (PanelLookAndFeel::captionFont());

    for (int length = 1; length <= seqMaxSteps; ++length)
        seqPatternLengthCombo.addItem (juce::String (length), length);

    seqPatternLengthCombo.onChange = [this]
    {
        params.seqPatternLength.store (seqPatternLengthCombo.getSelectedId(), std::memory_order_relaxed);
    };
    seqPatternLengthCombo.setSelectedId (seqMaxSteps, juce::dontSendNotification); // matches the atomic's own default
    seqPatternLengthCombo.onChange(); // seed, matching every attach helper's own seed call
    seqControlSection.addCell (seqPatternLengthLabel, seqPatternLengthCombo, false);

    wireKnobs (seqKnobs, seqKnobSpecs, numSeqKnobs, seqControlSection);

    // Lane select has no VoiceParameters target - see StepGrid::selectedLane's
    // own comment in the header - so it is wired directly rather than
    // through attachChoice, which requires one.
    seqLaneLabel.setText ("LANE", juce::dontSendNotification);
    seqLaneLabel.setJustificationType (juce::Justification::centred);
    seqLaneLabel.setFont (PanelLookAndFeel::captionFont());

    seqLaneCombo.addItem ("Pitch", 1);
    seqLaneCombo.addItem ("Cutoff", 2);
    seqLaneCombo.addItem ("Resonance", 3);
    seqLaneCombo.onChange = [this]
    {
        stepGrid.selectedLane = seqLaneCombo.getSelectedId() - 1;
        stepGrid.repaint();
    };
    seqLaneCombo.setSelectedId (1, juce::dontSendNotification);
    seqLaneCombo.onChange(); // seed, matching every attach helper's own seed call
    seqControlSection.addCell (seqLaneLabel, seqLaneCombo, false);

    // Item 10 (documents/character-and-vim.md). Toggle-stack cell first, same
    // precedent as ARP/SEQUENCER above.
    attachToggle (characterToggleStack.top, characterToggleSpecs[0], params);
    attachToggle (characterToggleStack.bottom, characterToggleSpecs[1], params);
    characterSection.addCell (characterToggleCaption, characterToggleStack);

    wireKnobs (characterKnobs, characterKnobSpecs, numCharacterKnobs, characterSection);

    for (auto* section : { &vcoSection, &vcfSection, &envSection, &lfoSection,
                            &keyboardSection, &arpSection, &outputSection, &seqControlSection,
                            &characterSection })
        addAndMakeVisible (*section);

    stepGrid.configure (params);
    addAndMakeVisible (stepGrid);

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
                    keyboardBaseNoteNumber + spec.semitoneOffset);

            pushNoteEvent ({ isDown ? NoteEvent::Type::NoteOn : NoteEvent::Type::NoteOff,
                              key.emittedNoteNumber,
                              pitchLog2HzForMidiNote (key.emittedNoteNumber),
                              1.0f });
        };

        pianoKeyboard.letters[(size_t) i].setText (juce::String::charToString ((juce::juce_wchar) spec.letter),
                                                     juce::dontSendNotification);
    }

    addAndMakeVisible (pianoKeyboard);

    audioSettingsButton.setButtonText ("Audio Settings");
    audioSettingsButton.setWantsKeyboardFocus (false);
    audioSettingsButton.onClick = [this]
    {
        if (onAudioSettingsClicked != nullptr)
            onAudioSettingsClicked();
    };
    addAndMakeVisible (audioSettingsButton);

    // Item 8 (documents/TODO.md) - header row, left of Audio Settings.
    // Amber text, matching the amber knob pointer this button drives -
    // TextButton::textColourOffId, not Label::textColourId (this is a
    // TextButton, which never consults a Label's ColourId).
    autovijiButton.setButtonText ("AUTOVIJI");
    autovijiButton.setColour (juce::TextButton::textColourOffId, PanelLookAndFeel::accentAlt);
    autovijiButton.setColour (juce::TextButton::textColourOnId, PanelLookAndFeel::accentAlt);
    autovijiButton.setWantsKeyboardFocus (false);
    autovijiButton.onClick = [this] { randomizeSequence(); };
    addAndMakeVisible (autovijiButton);

    // Item 9 (documents/settings-persistence-design.md section 6) - header
    // row, between Autoviji and Audio Settings. Plain onClick-only buttons,
    // same shape as audioSettingsButton/autovijiButton above.
    savePresetButton.setButtonText ("Save");
    savePresetButton.setWantsKeyboardFocus (false);
    savePresetButton.onClick = [this]
    {
        if (onSavePresetClicked != nullptr)
            onSavePresetClicked();
    };
    addAndMakeVisible (savePresetButton);

    loadPresetButton.setButtonText ("Load");
    loadPresetButton.setWantsKeyboardFocus (false);
    loadPresetButton.onClick = [this]
    {
        if (onLoadPresetClicked != nullptr)
            onLoadPresetClicked();
    };
    addAndMakeVisible (loadPresetButton);

    // The fixed design size, per section 3 - NOT the real window size. Step
    // 6's scale transform maps this onto whatever the actual window is.
    setSize (designWidth, designHeight);
}

void SynthPanel::refreshOctaveReadout()
{
    // dontSendNotification: this only PULLS the atomic's current value into
    // the knob's position (and, via textFromValueFunction, its text box) -
    // it must never fire onValueChange and write the same value straight
    // back, which would be harmless here but is the wrong shape to establish
    // for a UI -> atomic -> UI refresh path.
    octaveKnob.slider.setValue (params.masterOctaveShift.load (std::memory_order_relaxed),
                                 juce::dontSendNotification);
}

void SynthPanel::refreshControlsFromParameters()
{
    // Same 16-table shape as forEachSerializableParameter and the
    // constructor's own wireKnobs/wireChoices - see that method's comment
    // for why all three independently list the same tables rather than
    // sharing one true source: each does something different enough with
    // (widget, spec) that a shared visitor would need to carry more context
    // than it saves.
    auto knobs = [this] (auto& cells, const KnobSpec* specs, int count)
    {
        for (int i = 0; i < count; ++i)
            refreshKnob (cells[(size_t) i].slider, specs[i], params);
    };

    auto choices = [this] (auto& cells, const ChoiceSpec* specs, int count)
    {
        for (int i = 0; i < count; ++i)
            refreshChoice (cells[(size_t) i].comboBox, specs[i], params);
    };

    knobs (vcoKnobs, vcoKnobSpecs, numVcoKnobs);
    knobs (vcfKnobs, vcfKnobSpecs, numVcfKnobs);
    knobs (envKnobs, envKnobSpecs, numEnvKnobs);
    choices (envChoices, envChoiceSpecs, numEnvChoices);

    refreshToggle (lfoSyncToggle, lfoToggleSpecs[0], params);
    knobs (lfoKnobs, lfoKnobSpecs, numLfoKnobs);
    choices (lfoChoices, lfoChoiceSpecs, numLfoChoices);

    knobs (keyboardKnobs, keyboardKnobSpecs, numKeyboardKnobs);
    choices (keyboardChoices, keyboardChoiceSpecs, numKeyboardChoices);

    refreshToggle (arpToggleStack.top, arpToggleSpecs[0], params);
    refreshToggle (arpToggleStack.bottom, arpToggleSpecs[1], params);
    choices (arpChoices, arpChoiceSpecs, numArpChoices);
    knobs (arpKnobs, arpKnobSpecs, numArpKnobs);

    knobs (outputKnobs, outputKnobSpecs, numOutputKnobs);
    refreshOctaveReadout(); // the hand-wired Octave knob - not a KnobSpec cell

    refreshToggle (seqToggleStack.top, seqToggleSpecs[0], params);
    refreshToggle (seqToggleStack.bottom, seqToggleSpecs[1], params);
    choices (seqChoices, seqChoiceSpecs, numSeqChoices);
    knobs (seqKnobs, seqKnobSpecs, numSeqKnobs);

    refreshToggle (characterToggleStack.top, characterToggleSpecs[0], params);
    refreshToggle (characterToggleStack.bottom, characterToggleSpecs[1], params);
    knobs (characterKnobs, characterKnobSpecs, numCharacterKnobs);

    // Hand-wired, non-spec-table controls (see their own comments in the
    // constructor for why attachChoice doesn't cover them).
    seqPatternLengthCombo.setSelectedId (params.seqPatternLength.load (std::memory_order_relaxed),
                                          juce::dontSendNotification);
    // seqLaneCombo has no VoiceParameters target (UI-local lane selection) -
    // nothing for a preset load to disagree with.

    // The step grid reads VoiceParameters fresh on every paint (same
    // "never cache, always read live" convention currentStepForUi's own
    // comment describes) - a repaint is all a changed pattern needs.
    stepGrid.repaint();
}

SynthPanel::~SynthPanel()
{
    // Must happen before `lookAndFeel` is destroyed, or JUCE asserts on
    // shutdown - documents/ui-design.md section 7.
    setLookAndFeel (nullptr);
}

//==============================================================================
// documents/settings-persistence-design.md section 4. One table-walking
// lambda per spec kind (KnobSpec -> float, ChoiceSpec/ToggleSpec -> int),
// called once per existing static table below - the same 16 tables
// wireKnobs/wireChoices/attachToggle already walk in the constructor, listed
// here a second time rather than reused from there because the constructor's
// loop bodies also configure widgets, which this has no widgets to do.
void SynthPanel::forEachSerializableParameter (
    const std::function<void (const juce::String&, std::atomic<float> VoiceParameters::*)>& onFloat,
    const std::function<void (const juce::String&, std::atomic<int> VoiceParameters::*)>& onInt)
{
    auto knobs = [&onFloat] (const char* section, const KnobSpec* specs, int count)
    {
        for (int i = 0; i < count; ++i)
            onFloat (juce::String (section) + "." + specs[i].name, specs[i].target);
    };

    auto choices = [&onInt] (const char* section, const ChoiceSpec* specs, int count)
    {
        for (int i = 0; i < count; ++i)
            onInt (juce::String (section) + "." + specs[i].name, specs[i].target);
    };

    auto toggles = [&onInt] (const char* section, const ToggleSpec* specs, int count)
    {
        for (int i = 0; i < count; ++i)
            onInt (juce::String (section) + "." + specs[i].name, specs[i].target);
    };

    knobs   ("vco",      vcoKnobSpecs,      numVcoKnobs);
    knobs   ("vcf",      vcfKnobSpecs,      numVcfKnobs);
    knobs   ("env",      envKnobSpecs,      numEnvKnobs);
    choices ("env",      envChoiceSpecs,    numEnvChoices);
    knobs   ("lfo",      lfoKnobSpecs,      numLfoKnobs);
    choices ("lfo",      lfoChoiceSpecs,    numLfoChoices);
    toggles ("lfo",      lfoToggleSpecs,    numLfoToggles);
    knobs   ("keyboard", keyboardKnobSpecs, numKeyboardKnobs);
    choices ("keyboard", keyboardChoiceSpecs, numKeyboardChoices);
    knobs   ("arp",      arpKnobSpecs,      numArpKnobs);
    choices ("arp",      arpChoiceSpecs,    numArpChoices);
    toggles ("arp",      arpToggleSpecs,    numArpToggles);
    knobs   ("output",   outputKnobSpecs,   numOutputKnobs);
    knobs   ("seq",      seqKnobSpecs,      numSeqKnobs);
    choices ("seq",      seqChoiceSpecs,    numSeqChoices);
    toggles ("seq",      seqToggleSpecs,    numSeqToggles);
    knobs   ("character", characterKnobSpecs,   numCharacterKnobs);
    toggles ("character", characterToggleSpecs, numCharacterToggles);
}

//==============================================================================
// Item 8 (documents/TODO.md, documents/autoviji-design.md): fills all 16
// steps with a random note from two fixed octaves, rolls each step's gate
// (1-in-8 chance of coming up off) and per-step Cutoff lane, then turns the
// sequencer on. Runs entirely on the message thread (this is a button click
// handler) using the same plain-atomic storeStepValue helper
// StepCell::mouseDrag/mouseUp already use for these exact fields
// (ParameterControls.h) - no new threading pattern.
void SynthPanel::randomizeSequence()
{
    // Two fixed octaves, C2..B2 and C3..B3 (MIDI 36-59) - anchored on the
    // step grid's own existing default pitch
    // (VoiceParameters::defaultStepMidiNote), so a fresh random pattern
    // never lands in a surprising register.
    constexpr int randomOctaveBaseMidiNote = 36;

    auto& rng = juce::Random::getSystemRandom();

    for (int i = 0; i < seqMaxSteps; ++i)
    {
        const auto midiNote = randomOctaveBaseMidiNote + rng.nextInt (24);
        storeStepValue (&VoiceParameters::stepPitchLog2Hz, i, pitchLog2HzForMidiNote (midiNote), params);

        // 1-in-8 chance the step comes up off (silent); pitch above is
        // still written even for an off step - matches the grid's own
        // existing convention of gate and pitch being independent fields.
        const auto gateOn = rng.nextInt (8) != 0;
        storeStepValue (&VoiceParameters::stepGateOn, i, gateOn ? 1 : 0, params);

        storeStepValue (&VoiceParameters::stepCutoffNorm, i, rng.nextFloat(), params);
    }

    // Autoviji's own default groove: 1/8T, 8 steps - shorter and swung
    // against the plain 1/16 default, so a random pattern doesn't just sound
    // like the same straight grid with different notes. Same two-step shape
    // as the toggle sync below: store the atomic directly, then sync the
    // widget's own displayed state to match (attachChoice's onChange is
    // built to store FROM the widget, not the other way round).
    //
    // The combo's own id is NOT (int) StepDivision::EighthTriplet + 1 - seq's
    // Division combo is sliced (seqChoiceSpecs' own Division entry,
    // firstChoiceValue = StepDivision::Whole), so its ids are offset from the
    // enum's true global values. Same inverse attachChoice's own seeding
    // uses (ParameterControls.h): subtract firstChoiceValue back off before
    // adding the usual +1. Getting this wrong doesn't crash - it just hands
    // setSelectedId an id with no matching item, which JUCE quietly shows as
    // no selection at all rather than asserting - exactly what happened
    // before this fix.
    params.seqDivision.store ((int) StepDivision::EighthTriplet, std::memory_order_relaxed);
    seqChoices[0].comboBox.setSelectedId ((int) StepDivision::EighthTriplet
                                               - seqChoiceSpecs[0].firstChoiceValue + 1,
                                           juce::dontSendNotification);

    constexpr int autovijiPatternLength = 8;
    params.seqPatternLength.store (autovijiPatternLength, std::memory_order_relaxed);
    seqPatternLengthCombo.setSelectedId (autovijiPatternLength, juce::dontSendNotification);

    // Turn the sequencer on. Writing the atomic directly rather than going
    // through attachToggle's onClick (that lambda is what actually owns this
    // store) and then syncing the ToggleButton's own visual state to match -
    // same two-step shape attachToggle's own seed call uses, just from here
    // instead of the constructor.
    params.seqEnabled.store (1, std::memory_order_relaxed);
    seqToggleStack.top.setToggleState (true, juce::dontSendNotification);
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
    constexpr int autovijiWidth = 90; // item 8 - narrower than Audio Settings, "Autoviji" is shorter
    constexpr int autovijiGap = 12;   // same as the section-to-section `gap` below
    constexpr int presetButtonWidth = 64; // "Save"/"Load" are short - narrower than autovijiWidth
    constexpr int presetButtonGap = 12;   // same as autovijiGap
    // Item 7 build step 6: the pattern grid's own row height, chosen (not
    // derived from PanelSection's cell geometry - StepGrid has no caption
    // strip) to give a vertical-drag gesture a comfortable range, roughly
    // matching a knob cell's own control-row height.
    constexpr int stepGridHeight = 130;
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

        // Item 9 - between Autoviji and Audio Settings, same row. Removed
        // from the right in Load-then-Save order, which places them
        // left-to-right as Save (nearer Autoviji), Load (nearer Audio
        // Settings).
        headerRow.removeFromRight (presetButtonGap);
        loadPresetButton.setBounds (headerRow.removeFromRight (presetButtonWidth)
                                              .withSizeKeepingCentre (presetButtonWidth, audioSettingsHeight));
        headerRow.removeFromRight (presetButtonGap);
        savePresetButton.setBounds (headerRow.removeFromRight (presetButtonWidth)
                                              .withSizeKeepingCentre (presetButtonWidth, audioSettingsHeight));

        // Item 8 - immediately to Save's left, same row.
        headerRow.removeFromRight (autovijiGap);
        autovijiButton.setBounds (headerRow.removeFromRight (autovijiWidth)
                                            .withSizeKeepingCentre (autovijiWidth, audioSettingsHeight));

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

    // Section row B: LFO | KEYBOARD | ARP. OUTPUT used to close this row out
    // (four sections); now rides along on SEQUENCER's row instead - see
    // outputSection's own comment in SynthPanel.h for the full history.
    // These three sections' widthForCells sum plus two sectionGaps once
    // again totals exactly row A's own width (envRowWidthCompensation's own
    // comment) - designWidth's own comment in SynthPanel.h has the rest.
    {
        auto row = area.removeFromTop (PanelSection::heightForCells());
        place (row, lfoSection, 1 + numLfoKnobs + numLfoChoices); // 1 = the Sync toggle cell
        place (row, keyboardSection, numKeyboardKnobs + numKeyboardChoices);
        place (row, arpSection, 1 + numArpChoices + numArpKnobs); // 1 = the On/Hold stacked cell
    }
    area.removeFromTop (gap);

    // Item 7 build step 6, replacing the old reserved strip. SEQUENCER's
    // control cluster is left-aligned at its own natural width (like every
    // other section) rather than stretched to fill the row - not every row
    // needs to hit the 1240px budget exactly, only rows A/B did (section 3).
    // OUTPUT shares this row now, immediately to SEQUENCER's right - see
    // outputSection's own comment in SynthPanel.h for why it moved here.
    {
        auto row = area.removeFromTop (PanelSection::heightForCells());
        // 1 = the On/Record stacked cell (numSeqToggles is 2 controls but ONE
        // cell - same "hardcode 1, don't use the toggle count" precedent as
        // row B's arpSection line above). +2 = the hand-wired Pattern Length
        // and Lane cells - neither is a ChoiceSpec, see numSeqChoices' own
        // comment in SynthPanel.h.
        place (row, seqControlSection, 1 + numSeqChoices + numSeqKnobs + 2);

        // Item 10 (documents/character-and-vim.md) - inserted between
        // SEQUENCER and OUTPUT, per that doc's own "V1 build scope" width
        // arithmetic. 1 = the Vim/Chorus stacked toggle cell, same
        // "hardcode 1, not the toggle count" precedent as seqControlSection's
        // own line above.
        place (row, characterSection, 1 + numCharacterKnobs);

        place (row, outputSection, numOutputKnobs + 1); // +1 = the Octave cell
    }
    area.removeFromTop (gap);

    stepGrid.setBounds (area.removeFromTop (stepGridHeight));
    area.removeFromTop (gap);

    // PianoKeyboard lays out its own keys and letter row internally - see
    // PianoKeyboard::resized(). Audio Settings lives in the header, top-right
    // - see above; the latching note buttons are gone (removed entirely).
    // The Octave knob no longer sits here - it moved into outputSection, see
    // octaveKnob's own comment in SynthPanel.h - so the keyboard now claims
    // the whole row.
    {
        auto keyboardRow = area.removeFromTop (keyboardRowHeight);
        pianoKeyboard.setBounds (keyboardRow);
    }
}
