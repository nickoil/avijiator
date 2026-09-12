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

#include "PresetSerialization.h"

#include "../DSP/SynthVoice.h"
#include "../UI/ParameterControls.h"
#include "../UI/SynthPanel.h"

namespace PresetSerialization
{
    namespace
    {
        constexpr const char* formatVersionAttribute = "formatVersion";

        // One <Param key="..." value="..."/> child per SynthPanel::
        // forEachSerializableParameter entry, rather than using the key as
        // the XML ATTRIBUTE NAME directly (xml->setAttribute(key, ...)).
        // Several KnobSpec/ChoiceSpec display names contain characters an
        // XML attribute NAME cannot (a space in "Pulse Width"/"Glide Time"/
        // "Sync Division"/"Note Priority", "->" in "Env->Cutoff") - caught by
        // this project's cdb self-test convention as a real
        // JUCE_ASSERT (xml/juce_XmlElement.cpp:76) the first time this ran,
        // not guessed in advance. An attribute VALUE has no such
        // restriction, so the key travels as data instead.
        constexpr const char* paramTagName = "Param";
        constexpr const char* paramKeyAttribute = "key";
        constexpr const char* paramValueAttribute = "value";

        const juce::XmlElement* findParam (const juce::XmlElement& xml, const juce::String& key)
        {
            for (auto* param = xml.getChildByName (paramTagName); param != nullptr;
                 param = param->getNextElementWithTagName (paramTagName))
                if (param->getStringAttribute (paramKeyAttribute) == key)
                    return param;

            return nullptr;
        }

        // documents/settings-persistence-design.md section 2's 3 non-spec
        // scalars, plus masterOctaveShift - a 4th that post-dates the design
        // doc (documents/note-handling-design.md section 7's revision added
        // it as a hand-wired OUTPUT knob, not a KnobSpec, so
        // SynthPanel::forEachSerializableParameter never sees it either).
        constexpr const char* velocityToAmpDepthKey = "voice.velocityToAmpDepth";
        constexpr const char* velocityToCutoffDepthOctavesKey = "voice.velocityToCutoffDepthOctaves";
        constexpr const char* seqPatternLengthKey = "seq.patternLength";
        constexpr const char* masterOctaveShiftKey = "output.octave";

        constexpr const char* stepsTagName = "Steps";
        constexpr const char* stepTagName = "Step";
        constexpr const char* stepIndexAttribute = "index";
        constexpr const char* stepPitchAttribute = "pitchLog2Hz";
        constexpr const char* stepGateAttribute = "gate";
        constexpr const char* stepAccentAttribute = "accent";
        constexpr const char* stepSlideAttribute = "slide";
        constexpr const char* stepCutoffAttribute = "cutoffNorm";
        constexpr const char* stepResonanceAttribute = "resonanceNorm";

        constexpr const char* latchTagName = "ArpLatch";
        constexpr const char* latchNumLatchedAttribute = "numLatched";
        constexpr const char* latchAwaitingFreshChordAttribute = "awaitingFreshChord";
        constexpr const char* latchNoteTagName = "Note";
        constexpr const char* latchNoteIndexAttribute = "index";
        constexpr const char* latchNoteNumberAttribute = "noteNumber";
        constexpr const char* latchPitchAttribute = "pitchLog2Hz";
        constexpr const char* latchVelocityAttribute = "velocity";
    }

    std::unique_ptr<juce::XmlElement> toXml (const VoiceParameters& params, const Arpeggiator& arp)
    {
        auto xml = std::make_unique<juce::XmlElement> (rootTagName);
        xml->setAttribute (formatVersionAttribute, currentFormatVersion);

        // ~33 KnobSpec/ChoiceSpec/ToggleSpec-covered scalars, generically -
        // section 4. One <Param> child per entry - see paramTagName's own
        // comment above for why this isn't a plain attribute keyed by name.
        SynthPanel::forEachSerializableParameter (
            [&] (const juce::String& key, std::atomic<float> VoiceParameters::* target)
            {
                auto* param = xml->createNewChildElement (paramTagName);
                param->setAttribute (paramKeyAttribute, key);
                param->setAttribute (paramValueAttribute,
                                      (double) (params.*target).load (std::memory_order_relaxed));
            },
            [&] (const juce::String& key, std::atomic<int> VoiceParameters::* target)
            {
                auto* param = xml->createNewChildElement (paramTagName);
                param->setAttribute (paramKeyAttribute, key);
                param->setAttribute (paramValueAttribute, (params.*target).load (std::memory_order_relaxed));
            });

        // 4 hand-written non-spec scalars - section 2.
        xml->setAttribute (velocityToAmpDepthKey,
                            (double) params.velocityToAmpDepth.load (std::memory_order_relaxed));
        xml->setAttribute (velocityToCutoffDepthOctavesKey,
                            (double) params.velocityToCutoffDepthOctaves.load (std::memory_order_relaxed));
        xml->setAttribute (seqPatternLengthKey, params.seqPatternLength.load (std::memory_order_relaxed));
        xml->setAttribute (masterOctaveShiftKey, params.masterOctaveShift.load (std::memory_order_relaxed));

        // 6x16 step-sequencer arrays, one <Step> child per index - section 2.
        auto* steps = xml->createNewChildElement (stepsTagName);
        for (int i = 0; i < seqMaxSteps; ++i)
        {
            auto* step = steps->createNewChildElement (stepTagName);
            step->setAttribute (stepIndexAttribute, i);
            step->setAttribute (stepPitchAttribute,
                                 (double) loadStepValue (&VoiceParameters::stepPitchLog2Hz, i, params));
            step->setAttribute (stepGateAttribute, loadStepValue (&VoiceParameters::stepGateOn, i, params));
            step->setAttribute (stepAccentAttribute, loadStepValue (&VoiceParameters::stepAccent, i, params));
            step->setAttribute (stepSlideAttribute, loadStepValue (&VoiceParameters::stepSlide, i, params));
            step->setAttribute (stepCutoffAttribute,
                                 (double) loadStepValue (&VoiceParameters::stepCutoffNorm, i, params));
            step->setAttribute (stepResonanceAttribute,
                                 (double) loadStepValue (&VoiceParameters::stepResonanceNorm, i, params));
        }

        // The arp's latched (Hold) chord - section 5. Same serialized data
        // for both tiers, no special-casing (section 1).
        const auto latch = arp.getLatchSnapshot();
        auto* latchXml = xml->createNewChildElement (latchTagName);
        latchXml->setAttribute (latchNumLatchedAttribute, latch.numLatched);
        latchXml->setAttribute (latchAwaitingFreshChordAttribute, latch.awaitingFreshChord ? 1 : 0);

        for (int i = 0; i < latch.numLatched; ++i)
        {
            auto* note = latchXml->createNewChildElement (latchNoteTagName);
            note->setAttribute (latchNoteIndexAttribute, i);
            note->setAttribute (latchNoteNumberAttribute, (int) latch.noteNumbers[(size_t) i]);
            note->setAttribute (latchPitchAttribute, (double) latch.pitchesLog2Hz[(size_t) i]);
            note->setAttribute (latchVelocityAttribute, (double) latch.velocities[(size_t) i]);
        }

        return xml;
    }

    void fromXml (const juce::XmlElement& xml, VoiceParameters& params, Arpeggiator& arp)
    {
        // A saved field with no current match is silently ignored (the
        // visitor simply never gets asked about it); a current field with no
        // saved match is silently left as `params` already had it - the
        // `findParam` guard below is what makes that true, rather than
        // storing getDoubleAttribute/getIntAttribute's own 0 default.
        SynthPanel::forEachSerializableParameter (
            [&] (const juce::String& key, std::atomic<float> VoiceParameters::* target)
            {
                if (auto* param = findParam (xml, key))
                    (params.*target).store ((float) param->getDoubleAttribute (paramValueAttribute),
                                             std::memory_order_relaxed);
            },
            [&] (const juce::String& key, std::atomic<int> VoiceParameters::* target)
            {
                if (auto* param = findParam (xml, key))
                    (params.*target).store (param->getIntAttribute (paramValueAttribute),
                                             std::memory_order_relaxed);
            });

        if (xml.hasAttribute (velocityToAmpDepthKey))
            params.velocityToAmpDepth.store ((float) xml.getDoubleAttribute (velocityToAmpDepthKey),
                                              std::memory_order_relaxed);
        if (xml.hasAttribute (velocityToCutoffDepthOctavesKey))
            params.velocityToCutoffDepthOctaves.store ((float) xml.getDoubleAttribute (velocityToCutoffDepthOctavesKey),
                                                         std::memory_order_relaxed);
        if (xml.hasAttribute (seqPatternLengthKey))
            params.seqPatternLength.store (xml.getIntAttribute (seqPatternLengthKey), std::memory_order_relaxed);
        if (xml.hasAttribute (masterOctaveShiftKey))
            params.masterOctaveShift.store (xml.getIntAttribute (masterOctaveShiftKey), std::memory_order_relaxed);

        // getChildByName + getNextElementWithTagName rather than a range-for
        // child iterator - both long-standing, certain JUCE XmlElement API,
        // deliberately preferred over anything newer this project hasn't
        // already verified elsewhere (CLAUDE.md: never guess at JUCE API
        // behaviour).
        if (auto* steps = xml.getChildByName (stepsTagName))
        {
            for (auto* step = steps->getChildByName (stepTagName); step != nullptr;
                 step = step->getNextElementWithTagName (stepTagName))
            {
                const auto index = step->getIntAttribute (stepIndexAttribute, -1);
                if (index < 0 || index >= seqMaxSteps)
                    continue; // out-of-range index in a hand-edited file - ignored, not asserted

                storeStepValue (&VoiceParameters::stepPitchLog2Hz, index,
                                 (float) step->getDoubleAttribute (stepPitchAttribute), params);
                storeStepValue (&VoiceParameters::stepGateOn, index,
                                 step->getIntAttribute (stepGateAttribute), params);
                storeStepValue (&VoiceParameters::stepAccent, index,
                                 step->getIntAttribute (stepAccentAttribute), params);
                storeStepValue (&VoiceParameters::stepSlide, index,
                                 step->getIntAttribute (stepSlideAttribute), params);
                storeStepValue (&VoiceParameters::stepCutoffNorm, index,
                                 (float) step->getDoubleAttribute (stepCutoffAttribute), params);
                storeStepValue (&VoiceParameters::stepResonanceNorm, index,
                                 (float) step->getDoubleAttribute (stepResonanceAttribute), params);
            }
        }

        if (auto* latchXml = xml.getChildByName (latchTagName))
        {
            Arpeggiator::LatchSnapshot snapshot;
            snapshot.numLatched = juce::jlimit (0, NoteStack::maxHeldNotes,
                                                 latchXml->getIntAttribute (latchNumLatchedAttribute));
            snapshot.awaitingFreshChord = latchXml->getIntAttribute (latchAwaitingFreshChordAttribute, 1) != 0;

            for (auto* note = latchXml->getChildByName (latchNoteTagName); note != nullptr;
                 note = note->getNextElementWithTagName (latchNoteTagName))
            {
                const auto index = note->getIntAttribute (latchNoteIndexAttribute, -1);
                if (index < 0 || index >= snapshot.numLatched)
                    continue;

                snapshot.noteNumbers[(size_t) index] = (std::uint8_t) note->getIntAttribute (latchNoteNumberAttribute);
                snapshot.pitchesLog2Hz[(size_t) index] = (float) note->getDoubleAttribute (latchPitchAttribute);
                snapshot.velocities[(size_t) index] = (float) note->getDoubleAttribute (latchVelocityAttribute);
            }

            // Set even when numLatched is 0 (an explicitly-empty latch is
            // still a real value to load, not "nothing to do") - see
            // requestLatchLoad's own ordering-requirement comment for why
            // this must happen alongside arpEnabled/arpHold above, not
            // deferred.
            arp.requestLatchLoad (snapshot);
        }
    }

    juce::File getPresetsFolder()
    {
        // Same 4 Options fields MainComponent.cpp's own devicePropertiesOptions()
        // sets, not just folderName - an earlier version of this function left
        // applicationName/filenameSuffix at their empty defaults on the theory
        // that only folderName affects getDefaultFile()'s PARENT directory.
        // Verified wrong via the cdb self-test convention (a real launch wrote
        // no Presets folder at all) rather than assumed correct - matching
        // the exact, already-proven-working Options this codebase already
        // uses is what actually settles which folder getDefaultFile() picks.
        juce::PropertiesFile::Options options;
        options.applicationName = "Avijiator";
        options.filenameSuffix = "settings";
        options.folderName = "Avijiator";
        options.osxLibrarySubFolder = "Application Support";
        return options.getDefaultFile().getParentDirectory().getChildFile ("Presets");
    }
}

//==============================================================================
#if JUCE_DEBUG

void runPresetRoundTripSelfTest()
{
    using namespace PresetSerialization;

    //==========================================================================
    // SCENARIO 1: every generically-covered scalar, plus the 4 hand-written
    // ones and the 6 step arrays, round-trips exactly.
    {
        VoiceParameters source;
        Arpeggiator sourceArp; // empty latch - covered separately by scenario 2

        // Deterministic, per-field NON-DEFAULT values via the same generic
        // enumeration the serializer itself uses, rather than a hand-
        // maintained list of ~33 fields that could silently drift from the
        // real spec tables (section 10: an all-defaults fixture would let a
        // silent no-op pass). A running counter, distinct per call, is what
        // proves the display-name collisions ("Gate" in arp+seq, "Division"
        // in arp+seq, "On" in arp+seq) each keep their own value rather than
        // one silently overwriting the other through a shared XML key.
        auto floatCounter = 0;
        auto intCounter = 0;
        SynthPanel::forEachSerializableParameter (
            [&] (const juce::String&, std::atomic<float> VoiceParameters::* target)
            {
                (source.*target).store (1.0f + (float) floatCounter * 0.25f, std::memory_order_relaxed);
                ++floatCounter;
            },
            [&] (const juce::String&, std::atomic<int> VoiceParameters::* target)
            {
                (source.*target).store (intCounter, std::memory_order_relaxed);
                ++intCounter;
            });

        source.velocityToAmpDepth.store (0.42f, std::memory_order_relaxed);
        source.velocityToCutoffDepthOctaves.store (1.23f, std::memory_order_relaxed);
        source.seqPatternLength.store (11, std::memory_order_relaxed);
        source.masterOctaveShift.store (2, std::memory_order_relaxed);

        for (int i = 0; i < seqMaxSteps; ++i)
        {
            storeStepValue (&VoiceParameters::stepPitchLog2Hz, i, 5.0f + (float) i * 0.1f, source);
            storeStepValue (&VoiceParameters::stepGateOn, i, i % 2, source);
            storeStepValue (&VoiceParameters::stepAccent, i, (i + 1) % 2, source);
            storeStepValue (&VoiceParameters::stepSlide, i, (i % 3 == 0) ? 1 : 0, source);
            storeStepValue (&VoiceParameters::stepCutoffNorm, i, 0.1f * (float) i, source);
            storeStepValue (&VoiceParameters::stepResonanceNorm, i, 0.05f * (float) i, source);
        }

        // Explicitly excluded from serialization (section 2) - proven by
        // asserting it is NOT copied below, not merely by never checking it.
        source.currentStepForUi.store (7, std::memory_order_relaxed);

        const auto xml = toXml (source, sourceArp);

        VoiceParameters loaded; // fresh - a field this build no longer has
                                 // simply never gets visited by the loop below
        Arpeggiator loadedArp;
        fromXml (*xml, loaded, loadedArp);

        // Exact, not approximate (section 10) - an XML round-trip losing
        // float precision is itself a bug worth catching, matching this
        // codebase's existing exact-not-approximate self-test posture
        // (e.g. runArpTransitionSelfTest's silence floor).
        SynthPanel::forEachSerializableParameter (
            [&] (const juce::String&, std::atomic<float> VoiceParameters::* target)
            {
                jassert ((source.*target).load (std::memory_order_relaxed)
                             == (loaded.*target).load (std::memory_order_relaxed));
            },
            [&] (const juce::String&, std::atomic<int> VoiceParameters::* target)
            {
                jassert ((source.*target).load (std::memory_order_relaxed)
                             == (loaded.*target).load (std::memory_order_relaxed));
            });

        jassert (loaded.velocityToAmpDepth.load (std::memory_order_relaxed) == 0.42f);
        jassert (loaded.velocityToCutoffDepthOctaves.load (std::memory_order_relaxed) == 1.23f);
        jassert (loaded.seqPatternLength.load (std::memory_order_relaxed) == 11);
        jassert (loaded.masterOctaveShift.load (std::memory_order_relaxed) == 2);

        for (int i = 0; i < seqMaxSteps; ++i)
        {
            jassert (loadStepValue (&VoiceParameters::stepPitchLog2Hz, i, loaded)
                         == 5.0f + (float) i * 0.1f);
            jassert (loadStepValue (&VoiceParameters::stepGateOn, i, loaded) == i % 2);
            jassert (loadStepValue (&VoiceParameters::stepAccent, i, loaded) == (i + 1) % 2);
            jassert (loadStepValue (&VoiceParameters::stepSlide, i, loaded) == ((i % 3 == 0) ? 1 : 0));
            jassert (loadStepValue (&VoiceParameters::stepCutoffNorm, i, loaded) == 0.1f * (float) i);
            jassert (loadStepValue (&VoiceParameters::stepResonanceNorm, i, loaded) == 0.05f * (float) i);
        }

        // NOT touched by fromXml - stays at loaded's OWN fresh default (-1),
        // never overwritten with source's 7.
        jassert (loaded.currentStepForUi.load (std::memory_order_relaxed) == -1);
    }

    //==========================================================================
    // SCENARIO 2: the arp latch. A real Arpeggiator driven into a held-chord
    // state via its actual public API (resolveActiveNotes), never by
    // reaching into `latched` directly - snapshotted, injected into a
    // second, fresh Arpeggiator, and proven identical by RENDERED OUTPUT
    // (matching runArpTransitionSelfTest's own convention), not by comparing
    // internal state.
    {
        Arpeggiator sourceArp, targetArp;
        SynthVoice sourceVoice, targetVoice;

        // Identical voice configuration for both renders - tempo, division,
        // gate and pattern all matter to what gets rendered, so both sides
        // must agree on all four, not just share a starting VoiceParameters
        // default.
        auto configureVoice = [] (SynthVoice& voice)
        {
            auto& p = voice.getParameters();
            p.masterTempoBpm.store (120.0f, std::memory_order_relaxed);
            p.arpDivision.store ((int) StepDivision::Sixteenth, std::memory_order_relaxed);
            p.arpGateLength.store (0.5f, std::memory_order_relaxed);
            p.arpPattern.store ((int) ArpPattern::Up, std::memory_order_relaxed);
            p.arpHold.store (1, std::memory_order_relaxed); // Hold on - resolveActiveNotes must consult the latch
            voice.prepare (16000.0); // matches runArpTransitionSelfTest's own low test rate
        };

        configureVoice (sourceVoice);
        configureVoice (targetVoice);

        // BOTH arps' prepare() FIRST, before either latch is touched -
        // Arpeggiator::prepare() calls reset(), which clears numLatched/
        // latchAwaitingFreshChord. Setting up the latch before this would
        // have it silently wiped out from under the test a few lines later.
        sourceArp.prepare (16000.0);
        targetArp.prepare (16000.0);

        const NoteStack::HeldNote chord[] =
        {
            { 60, pitchLog2HzForMidiNote (60), 1.0f },
            { 64, pitchLog2HzForMidiNote (64), 1.0f },
            { 67, pitchLog2HzForMidiNote (67), 1.0f },
        };

        sourceArp.resolveActiveNotes ({ chord, 3 }, true); // Hold on, a fresh chord - T6

        const auto snapshot = sourceArp.getLatchSnapshot();
        jassert (snapshot.numLatched == 3);

        // Picked up at the top of targetArp's own next process() call below -
        // requestLatchLoad only arms a pending flag, untouched by prepare()/
        // reset(), so calling it after the prepare() calls above is safe.
        targetArp.requestLatchLoad (snapshot);

        // Empty liveNotes on both sides - the point of Hold is that the
        // latched chord keeps arpeggiating with nothing physically held.
        constexpr int numSamples = 64 * 8; // a handful of steps at 16kHz/120bpm/1-16th
        std::array<float, (size_t) numSamples> sourceOutput {}, targetOutput {};

        sourceArp.process (sourceVoice, {}, sourceOutput.data(), numSamples);
        targetArp.process (targetVoice, {}, targetOutput.data(), numSamples);

        // Exact - both renders start from silence with identical clocks and
        // an identical (if independently-sourced) latch, so there is no
        // legitimate reason for a single sample to differ.
        for (int i = 0; i < numSamples; ++i)
            jassert (sourceOutput[(size_t) i] == targetOutput[(size_t) i]);
    }

    //==========================================================================
    // SCENARIO 3: version-mismatch policy (section 2) - fromXml() given XML
    // missing a field the current build exposes leaves it at whatever the
    // target already held, silently, no migration shim, no warning. Proving
    // the ACCEPTED behaviour, not treating it as a bug to guard against.
    {
        VoiceParameters source;
        source.outputLevel.store (0.9f, std::memory_order_relaxed);
        source.masterTempoBpm.store (55.0f, std::memory_order_relaxed); // will be dropped below
        Arpeggiator sourceArp;

        auto xml = toXml (source, sourceArp);

        // Simulate "saved before masterTempoBpm existed" (or after a rename
        // away from it, per tempo-sync-design.md's own worked example) by
        // stripping its <Param> element out of a NORMAL save, rather than
        // hand-building an XML fragment against this file's own private
        // <Param>/key/value encoding (paramTagName etc., anonymous-
        // namespaced and not reachable from this free function).
        for (auto* param = xml->getChildByName ("Param"); param != nullptr;)
        {
            auto* next = param->getNextElementWithTagName ("Param");
            if (param->getStringAttribute ("key") == "output.Tempo")
            {
                xml->removeChildElement (param, true);
                break;
            }
            param = next;
        }

        VoiceParameters target;
        target.masterTempoBpm.store (77.0f, std::memory_order_relaxed); // a known, non-default marker
        Arpeggiator targetArp;

        fromXml (*xml, target, targetArp);

        jassert (target.outputLevel.load (std::memory_order_relaxed) == 0.9f); // the present field loaded
        jassert (target.masterTempoBpm.load (std::memory_order_relaxed) == 77.0f); // the absent one untouched
    }
}

#endif
