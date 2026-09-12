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

#include "PanelSection.h"

#include "PanelLookAndFeel.h"

//==============================================================================
PanelSection::PanelSection (juce::String title)
    : sectionTitle (std::move (title))
{
    // No inter-cell gap - see the class comment on why that's deliberate.
    grid.columnGap = juce::Grid::Px (0);
    grid.rowGap = juce::Grid::Px (0);
    grid.templateRows = { juce::Grid::TrackInfo (juce::Grid::Px (captionHeight)),
                           juce::Grid::TrackInfo (juce::Grid::Px (cellHeight - captionHeight)) };
}

void PanelSection::addCell (juce::Component& caption, juce::Component& control,
                             bool stretchControl, int extraColumnWidth)
{
    cells.push_back ({ &caption, &control, stretchControl });
    addAndMakeVisible (caption);
    addAndMakeVisible (control);

    grid.templateColumns.add (juce::Grid::TrackInfo (juce::Grid::Px (cellWidth + extraColumnWidth)));
    resized();
}

void PanelSection::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (PanelLookAndFeel::sectionFill);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (PanelLookAndFeel::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    auto titleBounds = getLocalBounds().reduced (edgePadding).removeFromTop (titleHeight);
    g.setColour (PanelLookAndFeel::textDim);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (sectionTitle, titleBounds, juce::Justification::centredLeft);
}

void PanelSection::resized()
{
    auto bounds = getLocalBounds().reduced (edgePadding);
    bounds.removeFromTop (titleHeight);
    bounds.removeFromTop (titleGap);

    // Fixed height for a non-stretched control (a combo box), centred in the
    // row rather than stretched to fill it - see the addCell comment in the
    // header.
    constexpr float compactControlHeight = 28.0f;

    juce::Array<juce::GridItem> items;

    for (size_t i = 0; i < cells.size(); ++i)
    {
        const auto column = (int) i + 1;
        const auto& cell = cells[i];

        items.add (juce::GridItem (*cell.caption).withColumn ({ column }).withRow ({ 1 }));

        auto controlItem = juce::GridItem (*cell.control).withColumn ({ column }).withRow ({ 2 });
        if (! cell.stretchControl)
            controlItem = controlItem.withHeight (compactControlHeight)
                                      .withAlignSelf (juce::GridItem::AlignSelf::center);

        items.add (controlItem);
    }

    grid.items = items;
    grid.performLayout (bounds);
}

//==============================================================================
int PanelSection::widthForCells (int numCells, int extraWidth)
{
    return edgePadding * 2 + cellWidth * numCells + extraWidth;
}

int PanelSection::heightForCells()
{
    return edgePadding * 2 + titleHeight + titleGap + cellHeight;
}
