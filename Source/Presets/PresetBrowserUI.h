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

#include <functional>

#include "../Arpeggiator.h"
#include "../DSP/VoiceParameters.h"

//==============================================================================
/*
    Tier B's user-facing save/load/browse/delete - documents/
    settings-persistence-design.md section 6 - plus the factory preset bank,
    section 9.

    Both dialogs need VoiceParameters& AND Arpeggiator& (PresetSerialization::
    toXml/fromXml's own two arguments), which is why MainComponent - not
    SynthPanel, which only owns VoiceParameters - owns the click handlers
    that call these. SynthPanel's Save/Load buttons only report the click
    (onSavePresetClicked/onLoadPresetClicked), same reasoning as
    onAudioSettingsClicked not reaching for the AudioDeviceManager itself.
*/
namespace PresetBrowserUI
{
    // Opens a small dialog: a name field, Save, Cancel. Rejects inline,
    // without closing, if the name matches an existing preset file - no
    // silent overwrite, no auto-suffixing (section 6, user's explicit call).
    void showSaveDialog (VoiceParameters& params, const Arpeggiator& arp);

    // Opens a dialog with a scrollable list of existing preset names, one
    // row each, with a Load action and a Delete action per row. `onLoaded`
    // fires right after a successful Load, i.e. right after `params`/`arp`
    // change underneath whatever's currently on screen - fromXml only
    // writes the atomics, so the caller still needs this to push those new
    // values back into its own widgets (SynthPanel::
    // refreshControlsFromParameters, wired in by MainComponent).
    void showLoadDialog (VoiceParameters& params, Arpeggiator& arp, std::function<void()> onLoaded);

    // Writes a small curated starting set into
    // PresetSerialization::getPresetsFolder() if that folder doesn't exist
    // yet (section 9) - i.e. first launch only. Call once at startup, before
    // the panel can be interacted with.
    void writeFactoryPresetsIfMissing();
}
