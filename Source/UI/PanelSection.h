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

#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/*
    One titled group of same-height cells, laid out left-to-right with
    juce::Grid. Cell geometry (88x116, a 16px caption strip) comes straight
    from documents/ui-design.md section 3 - the fixed design canvas's
    numbers, not anything this class invents. Cells sit edge-to-edge with NO
    internal gap; section 3's 88px-per-cell arithmetic already accounts for
    every pixel (76px knob + 6px either side) and only adds a gap BETWEEN
    sections, which is SynthPanel's job (step 5), not this class's.

    PanelSection doesn't know or care what's IN a cell - ParameterControls.h's
    attach helpers already fully configured each widget (style, range, the
    wiring) before it gets here. This class owns POSITION only: the section's
    title, fill, border, and where each cell's caption/control pair sits. It
    does not own the widgets it positions - the caller does (typically as
    array members, the same pattern the throwaway debug scaffolding already
    used), so addCell takes references, not unique_ptrs.
*/
class PanelSection final : public juce::Component
{
public:
    static constexpr int cellWidth = 88;
    static constexpr int cellHeight = 116;
    static constexpr int captionHeight = 20; // 16px text + 4px gap - section 3
    static constexpr int titleHeight = 18;
    static constexpr int titleGap = 6;
    static constexpr int edgePadding = 6;

    explicit PanelSection (juce::String title);

    // Registers one cell: `caption` fills the top captionHeight px, `control`
    // fills the rest of the cell. Cells lay out left-to-right in call order.
    // Calls addAndMakeVisible on both - the caller still owns them.
    //
    // stretchControl = false gives `control` a fixed height instead of the
    // full cellHeight-captionHeight control row, centred within it - a combo
    // box stretched to a knob's ~96px row reads as a giant text field, not a
    // dropdown.
    //
    // extraColumnWidth widens just THIS cell's column beyond cellWidth - see
    // the ENV section's Destination cell in SynthPanel.cpp: a section row
    // with one fewer section than its neighbour is short by exactly one
    // inter-section gap, and widening one cell is cheaper than a fake
    // section to fill it.
    void addCell (juce::Component& caption, juce::Component& control,
                  bool stretchControl = true, int extraColumnWidth = 0);

    void paint (juce::Graphics&) override;
    void resized() override;

    // For a section holding `numCells` knob/choice/toggle cells: the size
    // SynthPanel (step 5) should give this component, worked out from the
    // same fixed constants this class lays cells out with - so a section's
    // own size can never disagree with what it actually draws. Pass the same
    // extraWidth total spent across that section's addCell calls.
    static int widthForCells (int numCells, int extraWidth = 0);
    static int heightForCells();

private:
    juce::String sectionTitle;
    juce::Grid grid;

    // Non-owning - see the class comment. Kept only so resized() can rebuild
    // the grid's item list against the section's current bounds.
    struct Cell
    {
        juce::Component* caption;
        juce::Component* control;
        bool stretchControl;
    };
    std::vector<Cell> cells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelSection)
};
