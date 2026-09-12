/*
    This file is part of Avijiator.
    Copyright (C) 2026 Nick Casey

    Avijiator is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version.

    Avijiator is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with Avijiator. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/*
    Flat, dark "clean modern" look for the real instrument panel (item 6).
    See documents/ui-design.md sections 3-4 for the design record - canvas
    numbers, the palette table, and which JUCE ColourIds carry which token.

    The eleven static Colours below ARE the palette from section 4. They are
    the single place a literal hex value is allowed to appear; everything
    else - this class's own drawing methods, and PanelSection/SynthPanel once
    they exist in steps 3-5 - reads the palette through them (or, for the
    handful of standard widgets, through the ColourIds set once in the
    constructor) rather than repeating a colour literal. That is what makes a
    future palette change a one-file edit.

    Flat throughout: solid fills and single-pixel outlines, no gradients, no
    bevels, no drop shadows. State (hover/down/on) is communicated by a flat
    colour swap or brighter/darker variant of the same colour, never a
    gradient.
*/
class PanelLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    //==============================================================================
    // Palette tokens - documents/ui-design.md section 4.
    static const juce::Colour background;   // window ground, incl. letterbox bars
    static const juce::Colour panel;        // panel ground
    static const juce::Colour sectionFill;  // section backgrounds
    static const juce::Colour outline;      // section borders, combo box borders
    static const juce::Colour knobTrack;    // unfilled rotary/linear arc
    static const juce::Colour accent;       // filled arc, step-sequencer gate-on cells, toggle-on
    static const juce::Colour accentAlt;    // knob pointer, ARP active state, latched note buttons
    static const juce::Colour text;         // control labels, values
    static const juce::Colour textDim;      // section headers, units, SEQ placeholder
    static const juce::Colour keyWhite;     // on-screen keyboard naturals
    static const juce::Colour keyBlack;     // on-screen keyboard sharps

    // IBM Plex Sans, compiled in via CMakeLists.txt's juce_add_binary_data
    // rather than relying on the typeface being installed - see the .cpp.
    // Both cache their decoded Typeface, so any call site can ask for one
    // per Font construction with no repeated parsing cost. SemiBold is only
    // used by SynthPanel's header mark (documents/ui-mockup's
    // font-weight:600); everything else is Regular.
    static juce::Typeface::Ptr regularTypeface();
    static juce::Typeface::Ptr semiBoldTypeface();

    // The one correct way to build a Font from one of the two typefaces
    // above at a given height - see the .cpp for why "FontOptions(height)
    // .withTypeface(...)" (the obvious way to write this) is wrong: it
    // trips FontOptions' own internal assertion on every call. Also the
    // null-typeface fallback path, so a genuine embedded-font load failure
    // degrades to the system default font instead of crashing.
    static juce::Font fontFor (juce::Typeface::Ptr typeface, float height);

    // The 14pt Regular every knob/combo-box caption uses - see getLabelFont's
    // comment on why callers must now ask for this explicitly rather than
    // relying on it as a silent LookAndFeel default.
    static juce::Font captionFont();

    PanelLookAndFeel();

    //==============================================================================
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPos, float minSliderPos, float maxSliderPos,
                            const juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                        int buttonX, int buttonY, int buttonW, int buttonH,
                        juce::ComboBox&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                                const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted,
                                bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelLookAndFeel)
};
