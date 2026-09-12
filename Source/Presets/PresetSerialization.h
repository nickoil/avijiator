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

#include <memory>

#include <juce_core/juce_core.h>

#include "../Arpeggiator.h"
#include "../DSP/VoiceParameters.h"

//==============================================================================
/*
    Save/load for one instrument state - documents/settings-persistence-design.md,
    TODO item 9. Both persistence tiers this item builds call this same pair
    (section 3): Tier A (silent session-state auto-restore, MainComponent's
    ApplicationProperties) is simply an anonymous preset saved under a fixed
    key instead of a user-chosen filename; Tier B (named presets, the Save/
    Load dialogs in Source/Presets/PresetBrowserUI.h) writes/reads one file
    per preset in getPresetsFolder() below.

    A preset holds: every KnobSpec/ChoiceSpec/ToggleSpec-covered scalar
    (via SynthPanel::forEachSerializableParameter, section 4), 4 hand-written
    non-spec-table scalars, the 6x16 step-sequencer arrays, and the arp's
    latched (Hold) chord (section 5) - see toXml's own comment for the exact
    list. Explicitly excluded (section 2): currentStepForUi (a UI playhead,
    not a parameter), StepSequencer's own transient runtime state, and raw
    live-held-key state.
*/
namespace PresetSerialization
{
    // Exposed so a caller (the Load dialog, the factory bank) can sanity-
    // check a file's root tag before handing it to fromXml, without
    // duplicating the literal string. formatVersion is written from day
    // one but fromXml never branches on it (section 2) - it exists purely
    // for a human diffing a saved file later.
    constexpr const char* rootTagName = "AvijiatorPreset";
    constexpr int currentFormatVersion = 1;

    // Never returns null.
    std::unique_ptr<juce::XmlElement> toXml (const VoiceParameters& params, const Arpeggiator& arp);

    // Matches each saved field by NAME against whatever the current build
    // exposes (section 2's settled version/rename policy): a saved field
    // with no current match is silently ignored; a current field with no
    // saved match is silently left untouched (in practice, at whatever
    // default `params`/`arp` already held before this call - both tiers
    // always load into a fresh, just-constructed instance). No migration
    // shim, no warning - see the design doc for why that trade-off was
    // accepted rather than built around.
    void fromXml (const juce::XmlElement& xml, VoiceParameters& params, Arpeggiator& arp);

    // Where Tier B's named presets and the factory bank (documents/
    // settings-persistence-design.md section 9) both live - a `Presets`
    // subfolder next to the existing ApplicationProperties settings folder,
    // NOT embedded BinaryData (section 1's settled decision (b): editable in
    // place, no rebuild to tweak a preset).
    juce::File getPresetsFolder();

    // Fixed, arbitrary extension for one preset file, so the Load dialog's
    // directory scan and the Save dialog's write path always agree on what
    // counts as a preset.
    constexpr const char* fileExtension = ".avipreset";
}

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup - documents/
    settings-persistence-design.md section 10. A silent round-trip bug (a
    dropped field, a display-name collision quietly merging two different
    parameters, a float losing precision through the XML text form) would
    otherwise surface as "this preset sounds slightly different" weeks later,
    not as a crash.
*/
void runPresetRoundTripSelfTest();

#endif
