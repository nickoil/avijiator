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

#include "PanelLookAndFeel.h"

#include <BinaryData.h>

//==============================================================================
const juce::Colour PanelLookAndFeel::background  { 0xff14161a };
const juce::Colour PanelLookAndFeel::panel       { 0xff1c2026 };
const juce::Colour PanelLookAndFeel::sectionFill { 0xff22272e };
const juce::Colour PanelLookAndFeel::outline     { 0xff2e353f };
const juce::Colour PanelLookAndFeel::knobTrack   { 0xff333b45 };
const juce::Colour PanelLookAndFeel::accent      { 0xff4ec9c0 };
const juce::Colour PanelLookAndFeel::accentAlt   { 0xffe0a458 };
const juce::Colour PanelLookAndFeel::text        { 0xffe6eaf0 };
const juce::Colour PanelLookAndFeel::textDim     { 0xff8a94a6 };
const juce::Colour PanelLookAndFeel::keyWhite    { 0xffc9d1dc };
const juce::Colour PanelLookAndFeel::keyBlack    { 0xff2a313a };

//==============================================================================
juce::Typeface::Ptr PanelLookAndFeel::regularTypeface()
{
    static const juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::IBMPlexSansRegular_ttf, (size_t) BinaryData::IBMPlexSansRegular_ttfSize);
    // Was failing silently (see fontFor()'s comment) - turn a genuine
    // BinaryData/resource mismatch into a loud, debugger-visible failure
    // instead of a quiet fall-back to the system default font.
    jassert (typeface != nullptr);
    return typeface;
}

juce::Typeface::Ptr PanelLookAndFeel::semiBoldTypeface()
{
    static const juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::IBMPlexSansSemiBold_ttf, (size_t) BinaryData::IBMPlexSansSemiBold_ttfSize);
    jassert (typeface != nullptr);
    return typeface;
}

juce::Font PanelLookAndFeel::fontFor (juce::Typeface::Ptr typeface, float height)
{
    // "FontOptions (height).withTypeface (typeface)" - the obvious way to
    // write this - trips withTypeface()'s own internal jassert on every
    // single call: FontOptions(height) already carries a non-empty default
    // "Regular" style (from Font::plain), and withTypeface() asserts that
    // style is empty before it discards it in favour of the typeface's own.
    // Confirmed via cdb (see cdb-headless-assertion-check in memory) that the
    // typeface argument itself is never null when this fires - the assert is
    // about the discarded style field, not a load failure. Building from the
    // typeface first sidesteps it entirely: that constructor populates name/
    // style from the typeface itself, so nothing is later overwritten.
    if (typeface == nullptr)
        return juce::Font (juce::FontOptions (height));

    return juce::Font (juce::FontOptions (typeface).withHeight (height));
}

namespace
{
    // One font call site for every label/combo-box/toggle caption, so the
    // typeface and weight only need to change here.
    juce::Font uiFont (float height)
    {
        return PanelLookAndFeel::fontFor (PanelLookAndFeel::regularTypeface(), height);
    }
}

//==============================================================================
PanelLookAndFeel::PanelLookAndFeel()
{
    // Section 4's ColourId table, set once. Everything these methods draw
    // that ISN'T one of these eight standard widgets reads the static
    // palette constants directly instead - there's no ColourId for "section
    // fill" or "knob pointer".
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, knobTrack);
    setColour (juce::ComboBox::backgroundColourId, sectionFill);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, sectionFill);
    setColour (juce::ToggleButton::tickColourId, accent);
}

//==============================================================================
void PanelLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto centre = bounds.getCentre();
    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    constexpr float trackThickness = 4.0f;
    auto arcRadius = radius - trackThickness * 0.5f;

    // Unfilled track - flat stroke, no bevel.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, juce::PathStrokeType (trackThickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    // Filled portion, start angle to current value.
    if (toAngle > rotaryStartAngle)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                              rotaryStartAngle, toAngle, true);
        g.setColour (findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, juce::PathStrokeType (trackThickness, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    // Knob body - flat fill circle, single-pixel outline, no gradient/bevel.
    auto knobRadius = arcRadius - trackThickness;
    g.setColour (panel);
    g.fillEllipse (centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);
    g.setColour (outline);
    g.drawEllipse (centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.0f);

    // Pointer - single flat line from centre to the current angle. Deliberately
    // NOT rotarySliderFillColourId (teal, shared with the fill arc above) - the
    // pointer reads as a distinct "exact position" mark against the arc's
    // "how far turned" fill, so it gets its own colour straight from the
    // static palette, same as the section-4 comment's carve-out for anything
    // that isn't one of the eight standard widgets.
    auto pointerLength = knobRadius * 0.75f;
    juce::Path pointer;
    pointer.startNewSubPath (centre);
    pointer.lineTo (centre.getPointOnCircumference (pointerLength, toAngle));
    g.setColour (accentAlt);
    g.strokePath (pointer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
}

//==============================================================================
void PanelLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                          const juce::Slider::SliderStyle style, juce::Slider&)
{
    // Not used by any of item 6's 27 controls (all became rotary knobs - see
    // documents/ui-design.md section 2) but kept flat and complete since
    // JUCE's own AudioDeviceSelectorComponent (opened from Audio Settings)
    // can fall back to this LookAndFeel for its own sliders. Reuses the
    // rotary slider's two ColourIds rather than introducing new ones section
    // 4 doesn't list.
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    constexpr float trackThickness = 4.0f;
    constexpr float thumbSize = 12.0f;

    auto trackColour = findColour (juce::Slider::rotarySliderOutlineColourId);
    auto fillColour = findColour (juce::Slider::rotarySliderFillColourId);

    if (style == juce::Slider::LinearVertical)
    {
        auto trackX = bounds.getCentreX();
        juce::Rectangle<float> fullTrack (trackX - trackThickness * 0.5f, bounds.getY(),
                                           trackThickness, bounds.getHeight());
        g.setColour (trackColour);
        g.fillRoundedRectangle (fullTrack, trackThickness * 0.5f);

        auto filled = fullTrack.withTop (sliderPos);
        g.setColour (fillColour);
        g.fillRoundedRectangle (filled, trackThickness * 0.5f);

        g.setColour (panel);
        g.fillEllipse (trackX - thumbSize * 0.5f, sliderPos - thumbSize * 0.5f, thumbSize, thumbSize);
        g.setColour (outline);
        g.drawEllipse (trackX - thumbSize * 0.5f, sliderPos - thumbSize * 0.5f, thumbSize, thumbSize, 1.0f);
    }
    else
    {
        auto trackY = bounds.getCentreY();
        juce::Rectangle<float> fullTrack (bounds.getX(), trackY - trackThickness * 0.5f,
                                           bounds.getWidth(), trackThickness);
        g.setColour (trackColour);
        g.fillRoundedRectangle (fullTrack, trackThickness * 0.5f);

        auto filled = fullTrack.withRight (sliderPos);
        g.setColour (fillColour);
        g.fillRoundedRectangle (filled, trackThickness * 0.5f);

        g.setColour (panel);
        g.fillEllipse (sliderPos - thumbSize * 0.5f, trackY - thumbSize * 0.5f, thumbSize, thumbSize);
        g.setColour (outline);
        g.drawEllipse (sliderPos - thumbSize * 0.5f, trackY - thumbSize * 0.5f, thumbSize, thumbSize, 1.0f);
    }
}

//==============================================================================
void PanelLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                      int buttonX, int buttonY, int buttonW, int buttonH,
                                      juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat();
    g.setColour (findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    // Flat downward triangle in place of an arrow glyph/image.
    juce::Rectangle<float> arrowZone ((float) buttonX, (float) buttonY, (float) buttonW, (float) buttonH);
    auto arrowBounds = arrowZone.reduced (arrowZone.getWidth() * 0.3f, arrowZone.getHeight() * 0.38f);
    juce::Path arrow;
    arrow.addTriangle (arrowBounds.getX(), arrowBounds.getY(),
                        arrowBounds.getRight(), arrowBounds.getY(),
                        arrowBounds.getCentreX(), arrowBounds.getBottom());
    g.setColour (box.isEnabled() ? textDim : textDim.withAlpha (0.4f));
    g.fillPath (arrow);
}

//==============================================================================
void PanelLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                          bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/)
{
    // A flat tick box, not an iOS-style switch - matches the rest of the
    // panel's square, hard-edged controls. Only the box is drawn here; the
    // "On"/"Hold" captions come from a sibling Label the way a knob's caption
    // does (documents/ui-design.md section 3's cell layout), so this method
    // doesn't assume the button carries its own text.
    constexpr float boxSize = 18.0f;

    // Inset from the button's own left edge - without this the box sits
    // flush at x=0, and the highlight ring below (box.expanded) draws
    // 1.5px further left than that, which JUCE clips away since a
    // component never paints outside its own bounds. The clipped ring
    // reads as "the left edge disappears on hover".
    auto bounds = button.getLocalBounds().toFloat().withTrimmedLeft (2.0f);
    auto box = bounds.removeFromLeft (boxSize).withSizeKeepingCentre (boxSize, boxSize);

    auto tickColour = findColour (juce::ToggleButton::tickColourId);
    g.setColour (button.getToggleState() ? tickColour : sectionFill);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);

    if (shouldDrawButtonAsHighlighted)
    {
        g.setColour (tickColour.withAlpha (0.4f));
        g.drawRoundedRectangle (box.expanded (1.5f), 4.0f, 1.5f);
    }

    if (button.getButtonText().isNotEmpty())
    {
        auto textBounds = bounds.withTrimmedLeft (6.0f);
        g.setColour (text);
        g.setFont (uiFont (14.0f));
        g.drawText (button.getButtonText(), textBounds, juce::Justification::centredLeft);
    }
}

//==============================================================================
void PanelLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                              const juce::Colour& backgroundColour,
                                              bool shouldDrawButtonAsHighlighted,
                                              bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat();

    // Flat fill, state communicated by a flat brighter/darker swap - never a
    // gradient.
    auto fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.darker (0.2f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.15f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
}

//==============================================================================
juce::Font PanelLookAndFeel::captionFont()
{
    return uiFont (14.0f);
}

// Defers entirely to whatever the Label itself carries - it must ask, via
// captionFont() (attachKnob/attachChoice do) or its own juce::Font, for
// anything to appear. Deliberately NOT "return uiFont (14.0f)" any more: that
// blanket override was applied on TOP of every label.setFont() call, so any
// caller that customised a label's size (SynthPanel's tagline, the on-screen
// keyboard's QWERTY-letter captions, the octave readout/shortcuts) had that
// choice silently discarded at paint time - getFont() kept reporting the
// size that was set, but drawLabel() never actually used it. A label that
// never calls setFont() falls back to JUCE's own plain default font, which
// is why every such caller now sets one explicitly.
juce::Font PanelLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

juce::Font PanelLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return uiFont (14.0f);
}
