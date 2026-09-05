#include "MainComponent.h"
#include "StepSequencer.h"

#include <cmath>

#include "Presets/PresetBrowserUI.h"
#include "Presets/PresetSerialization.h"

//==============================================================================
namespace
{
    // Where the saved audio/MIDI device state (see appProperties) lives on
    // disk - documents/TODO.md's "Remember audio/MIDI device settings" item.
    juce::PropertiesFile::Options devicePropertiesOptions()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "Avijiator";
        options.filenameSuffix      = "settings";
        options.folderName          = "Avijiator";
        options.osxLibrarySubFolder = "Application Support";
        return options;
    }

    constexpr const char* audioDeviceStateKey = "audioDeviceState";

    // Tier A (documents/settings-persistence-design.md section 3) - the
    // same appProperties instance, a second key alongside the device-state
    // one above. An anonymous preset, saved under a fixed key instead of a
    // user-chosen filename - it and Tier B's named presets share the exact
    // same toXml/fromXml pair.
    constexpr const char* synthStateKey = "synthState";

    // Split out from the callback so it can be tested without MIDI hardware -
    // the conversion is the part most likely to be subtly wrong, and wrong
    // here is silent. Returns false for anything that isn't a note message.
    bool noteEventFromMidiMessage (const juce::MidiMessage& message, NoteEvent& result)
    {
        const auto noteNumber = message.getNoteNumber();

        // Written to be correct regardless of how JUCE's isNoteOn/isNoteOff
        // treat the running-status convention (a note-on with velocity 0
        // meaning note-off), rather than depending on their default
        // arguments.
        if (message.isNoteOn() && message.getVelocity() > 0)
        {
            result = { NoteEvent::Type::NoteOn,
                       (std::uint8_t) noteNumber,
                       pitchLog2HzForMidiNote (noteNumber),
                       message.getFloatVelocity() };
            return true;
        }

        if (message.isNoteOff() || (message.isNoteOn() && message.getVelocity() == 0))
        {
            result = { NoteEvent::Type::NoteOff,
                       (std::uint8_t) noteNumber,
                       pitchLog2HzForMidiNote (noteNumber),
                       0.0f };
            return true;
        }

        // Everything else - CC, pitch bend, aftertouch, clock - is ignored.
        // Mod wheel and aftertouch routing are character-and-vim.md B5.
        return false;
    }

   #if JUCE_DEBUG
    void runMidiConversionSelfTest()
    {
        NoteEvent event;

        // A normal note-on.
        jassert (noteEventFromMidiMessage (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), event));
        jassert (event.type == NoteEvent::Type::NoteOn);
        jassert (event.noteNumber == 60);
        jassert (event.velocity > 0.0f);

        // Middle C is 261.63Hz; log2 of that is ~8.03. Confirms the note
        // number actually reached the pitch conversion.
        jassert (std::abs (event.pitchLog2Hz - 8.0313f) < 0.01f);

        // A normal note-off.
        jassert (noteEventFromMidiMessage (juce::MidiMessage::noteOff (1, 60), event));
        jassert (event.type == NoteEvent::Type::NoteOff);
        jassert (event.noteNumber == 60);

        // THE RUNNING-STATUS CASE: a note-on with velocity 0 is a note-off.
        // Getting this wrong means every note from a device using running
        // status sticks on forever - silent until it happens, then baffling.
        jassert (noteEventFromMidiMessage (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 0), event));
        jassert (event.type == NoteEvent::Type::NoteOff);
        jassert (event.noteNumber == 64);

        // Non-note messages are rejected rather than turned into stray notes.
        jassert (! noteEventFromMidiMessage (juce::MidiMessage::controllerEvent (1, 1, 64), event));
        jassert (! noteEventFromMidiMessage (juce::MidiMessage::pitchWheel (1, 8192), event));
        jassert (! noteEventFromMidiMessage (juce::MidiMessage::allNotesOff (1), event));
    }
   #endif
}

MainComponent::MainComponent()
    // qwertyInput takes voice.getParameters() by reference, same reasoning
    // as panel below - both need VoiceParameters::masterOctaveShift, the
    // shared global octave transpose (documents/note-handling-design.md
    // section 7's revision).
    //
    // pushNoteEvent closes over router, which must already be constructed -
    // see the member comment in MainComponent.h. It is only ever CALLED
    // later, from the message thread, never during construction.
    : qwertyInput (voice.getParameters())
    , panel (voice.getParameters(), [this] (const NoteEvent& event) { router.pushUiEvent (event); })
{
   #if JUCE_DEBUG
    // All three of these fail SILENTLY when wrong - a dropped note event, a
    // wrong fallback note that just sounds like a playing mistake, or a
    // note-on-velocity-0 misread as a note-on and stuck forever. Asserted at
    // startup rather than left to be noticed by ear much later.
    runNoteEventFifoSelfTest();
    runNoteStackSelfTest();
    runMidiConversionSelfTest();

    // Clock drift is the one failure mode in this project that cannot be
    // caught by ear AT ALL - it is ~0.1ms per second, below human timing
    // jitter - so this assertion is the only real check that exists for it.
    runStepClockSelfTest();

    // The arp walker fails quietly in a different way: a skipped or stuttered
    // note when the held chord changes sounds like a playing mistake. The
    // changing-set recipes are asserted here rather than hunted for by ear.
    runArpPatternSelfTest();

    // A stuck note is not a property of the clock, the walker or the router -
    // it is a property of the SEAM between them, reached by a toggle order
    // rather than by any one call being wrong. So this one drives whole blocks
    // through the real hand-over and asserts silence in the rendered output.
    runArpTransitionSelfTest();

    // Item 7, build step 1: storage only, no render loop yet - see
    // documents/step-sequencer-design.md sections 4 and 10.
    runStepSequencerPatternSelfTest();

    // Item 7, build step 2: the render loop itself, driven through a real
    // SynthVoice with a fixed test pattern - not yet reachable from this
    // class's own getNextAudioBlock, since the arp/seq/keys hand-over is
    // build step 4. See documents/step-sequencer-design.md sections 3 and 10.
    runStepSequencerRenderSelfTest();

    // Item 7, build step 3: currentVelocity's two new summing points inside
    // SynthVoice itself (amp and cutoff) - independent of the sequencer, so
    // this exercises SynthVoice directly rather than through StepSequencer.
    // No UI knob yet (see VoiceParameters.h's comment on
    // velocityToAmpDepth/velocityToCutoffDepthOctaves) - step 6 is where the
    // panel's row-B width budget gets touched to make room for one.
    runAccentDepthSelfTest();

    // Item 7, build step 4: the 3-way keys/arp/seq hand-over itself - a stuck
    // note here is a property of the SEAM between all four collaborators, not
    // any one of them, same reasoning as runArpTransitionSelfTest. This class's
    // own getNextAudioBlock now actually reaches StepSequencer::process, via
    // the same renderVoiceBlock that test drives.
    runSeqTransitionSelfTest();

    // Item 7, build step 5: setStepFilterModulation's two new summing points
    // inside SynthVoice itself (cutoff and the brand-new resonance one) -
    // independent of the sequencer, so this exercises SynthVoice directly,
    // same split as build step 3's runAccentDepthSelfTest above.
    runFilterAutomationSelfTest();

    // Item 7, build step 7 (pitch entry): StepSequencer::process's new
    // liveResolution parameter and the record-arm capture it gates - see
    // documents/step-sequencer-design.md section 8. Step 6 added no new
    // audio self-test (pure UI), so this is the next one after step 5's
    // above.
    runStepRecordSelfTest();

    // Tempo sync: the LFO-sync computation added inside SynthVoice itself
    // (documents/tempo-sync-design.md section 3) - independent of the arp/
    // seq, so this exercises SynthVoice directly, same split as
    // runAccentDepthSelfTest/runFilterAutomationSelfTest above.
    runLfoTempoSyncSelfTest();

    // ChoiceSpec::firstChoiceValue's offset arithmetic (ParameterControls.h)
    // - the mechanism behind arp/seq's Division combo now showing a slice of
    // a larger shared list (documents/tempo-sync-design.md's follow-up
    // work), rather than the full StepDivision range LFO's own Sync Division
    // combo sees.
    runAttachChoiceOffsetSelfTest();

    // Item 9: a silent round-trip bug (a dropped field, a display-name
    // collision quietly merging two different parameters, a float losing
    // precision through the XML text form) would otherwise surface as "this
    // preset sounds slightly different" weeks later, not as a crash - see
    // documents/settings-persistence-design.md section 10.
    runPresetRoundTripSelfTest();

    // Item 10 (character-and-vim.md), A2: Adsr::setCurveEnabled's two
    // branches - the OFF path must stay byte-identical to the linear
    // arithmetic that predates this item, and the ON path must actually
    // reach its target rather than asymptoting forever.
    runAdsrCurveSelfTest();

    // Item 10, A1: Vcf::driveSaturate must be exactly softClip(x) at
    // driveAmount == 0, and must audibly change the filter's output once
    // turned up.
    runVcfDriveSelfTest();

    // Item 10, B2: the arp's and the sequencer's humanise onset-delay
    // scheduling (the "third deadline") - byte-identical at humaniseAmount
    // == 0, and no stuck note introduced by the extra scheduling state.
    runArpHumaniseSelfTest();
    runSeqHumaniseSelfTest();

    // Item 10, B1: Chorus produces a genuinely stereo, bounded, finite
    // signal - the piece getNextAudioBlock's chorusEnabled branch depends on.
    runChorusSelfTest();

    // Item 10, A3: OscillatorDrift stays bounded/finite, and two
    // independently-seeded instances actually diverge.
    runOscillatorDriftSelfTest();

    // Item 10, A6/B4: curvedVelocity's endpoints/monotonicity, and
    // CharacterProcessor's exact pass-through when disabled vs. its bounded,
    // asymmetric, noise-floor-bearing output when enabled.
    runCharacterProcessorSelfTest();

    // Item 10, A3/A6 integration: vimEnabled reaching SynthVoice's real
    // render path - byte-identical at the default, genuinely different once
    // turned on.
    runVimCharacterSelfTest();
   #endif

    // Item 9, section 9: a curated starting set, written once, only if the
    // Presets folder doesn't exist yet. Before Tier A's load below is fine
    // either way - the two are independent (Tier A lives in appProperties,
    // this writes into PresetSerialization::getPresetsFolder()).
    PresetBrowserUI::writeFactoryPresetsIfMissing();

    qwertyInput.onNoteEvent = [this] (const NoteEvent& event)
    {
        router.pushUiEvent (event);
    };

    panel.onAudioSettingsClicked = [this] { showAudioSettings(); };

    // Item 9, section 6 - each only reports its click, same reasoning as
    // onAudioSettingsClicked above not reaching for the AudioDeviceManager
    // itself: the dialogs need voice.getParameters() AND arp, which
    // SynthPanel doesn't own.
    panel.onSavePresetClicked = [this] { PresetBrowserUI::showSaveDialog (voice.getParameters(), arp); };
    panel.onLoadPresetClicked = [this]
    {
        PresetBrowserUI::showLoadDialog (voice.getParameters(), arp,
                                          [this] { panel.refreshControlsFromParameters(); });
    };

    addAndMakeVisible (panel);

    // The component itself takes keyboard focus so QWERTY note input works
    // as soon as the window is up - every mouse-driven control on the panel
    // explicitly declines focus (documents/ui-design.md section 6.1), so
    // clicking one never steals it away.
    setWantsKeyboardFocus (true);

    // Matches the design canvas 1:1 at startup - the window is resizable
    // (Main.cpp's setResizable), and resized() below scales the panel to fit
    // whatever size it becomes from here.
    setSize (SynthPanel::designWidth, SynthPanel::designHeight);

    appProperties.setStorageParameters (devicePropertiesOptions());
    std::unique_ptr<juce::XmlElement> savedDeviceState (
        appProperties.getUserSettings()->getXmlValue (audioDeviceStateKey));

    // Item 9, Tier A (documents/settings-persistence-design.md section 3) -
    // loaded here, after device state and before setAudioChannels below
    // starts the audio callbacks, into voice/arp (both already constructed -
    // see the member order in MainComponent.h). Nothing saved yet on
    // first-ever launch leaves voice/arp exactly at their own in-class-
    // initializer defaults, same as a fresh VoiceParameters/Arpeggiator
    // always start.
    if (std::unique_ptr<juce::XmlElement> savedSynthState (
            appProperties.getUserSettings()->getXmlValue (synthStateKey));
        savedSynthState != nullptr && savedSynthState->hasTagName (PresetSerialization::rootTagName))
    {
        PresetSerialization::fromXml (*savedSynthState, voice.getParameters(), arp);

        // fromXml only writes the atomics - panel's widgets (already
        // constructed and self-seeded to their own in-class defaults, via
        // the init list above) need pushing back into sync separately, or
        // the panel would silently show last session's DEFAULTS rather than
        // what was actually just restored. Same gap a Tier B Load hit first
        // - see SynthPanel::refreshControlsFromParameters' own comment.
        panel.refreshControlsFromParameters();
    }

    // Passing the saved state (nullptr on first-ever launch) restores the
    // same audio device *and* the same set of enabled MIDI inputs as last
    // session - see documents/TODO.md's "Remember audio/MIDI device
    // settings" item.
    setAudioChannels (0, 2, savedDeviceState.get());

    if (savedDeviceState == nullptr)
        enableAllMidiInputs(); // nothing saved yet - fall back to today's behaviour

    // An empty identifier registers as a catch-all across every ENABLED
    // input, so devices switched on later in the settings panel are picked up
    // without re-registering per device. Needed either way, whether the
    // enabled set came from enableAllMidiInputs() above or from savedDeviceState.
    deviceManager.addMidiInputDeviceCallback ({}, this);
}

void MainComponent::enableAllMidiInputs()
{
    // Enable every MIDI input that's present, rather than making the user go
    // and tick one before anything plays. For a synth, receiving from
    // whatever is plugged in is the sane default; the Audio Settings panel
    // can untick any that aren't wanted.
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        deviceManager.setMidiInputDeviceEnabled (device.identifier, true);
}

void MainComponent::handleIncomingMidiMessage (juce::MidiInput* /*source*/,
                                                const juce::MidiMessage& message)
{
    // MIDI THREAD. Pushing into the FIFO is all that happens here - never
    // touch the voice directly, and never allocate or lock.
    NoteEvent event;

    if (noteEventFromMidiMessage (message, event))
        router.pushMidiEvent (event);
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        deviceManager,
        0, 0,     // no audio inputs - this is a synth
        1, 2,     // mono or stereo out
        true,     // DO show MIDI inputs - this is where a device gets picked
        false,    // no MIDI output selector - nothing sends MIDI
        true,     // channels as stereo pairs
        false);   // show the advanced options (buffer size) rather than hiding them

    selector->setSize (500, 450);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    // Self-owning: the window deletes itself when closed.
    options.launchAsync();
}

bool MainComponent::keyStateChanged (bool /*isKeyDown*/)
{
    qwertyInput.pollKeyStates();

    // Refreshes the OUTPUT panel's readout so a comma/period press moves it
    // exactly as an Octave button click would - both drive the same shared
    // VoiceParameters::masterOctaveShift atomic now.
    panel.refreshOctaveReadout();

    // Not consumed: this only observes key state, so anything else that wants
    // these keys should still get them.
    return false;
}

void MainComponent::focusLost (FocusChangeType /*cause*/)
{
    qwertyInput.releaseAllHeldKeys();
}

void MainComponent::visibilityChanged()
{
    // See the declaration comment in MainComponent.h - this replaces the old
    // "click background first" reliance now that SynthPanel covers every
    // clickable pixel. isShowing() guards against grabbing focus on the
    // transient becomes-invisible-then-visible flicker some platforms send
    // during window setup, before there's a real peer to grab focus onto.
    if (isShowing())
        grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
    // Snapshot the live audio/MIDI device setup before any of the teardown
    // below can change it, so next launch restores exactly what was active
    // this session - documents/TODO.md's "Remember audio/MIDI device
    // settings" item.
    appProperties.getUserSettings()->setValue (audioDeviceStateKey,
                                                deviceManager.createStateXml().get());

    // Item 9, Tier A - same instance, same save point, second key. voice and
    // arp are still fully alive here (destroyed only after this destructor
    // body returns, in reverse declaration order).
    appProperties.getUserSettings()->setValue (
        synthStateKey, PresetSerialization::toXml (voice.getParameters(), arp).get());

    appProperties.saveIfNeeded();

    // Unregister BEFORE shutdownAudio, for the same reason shutdownAudio is
    // called here at all: a MIDI message arriving mid-destruction would call
    // into a partially destroyed object.
    deviceManager.removeMidiInputDeviceCallback ({}, this);

    // Must be called from the derived destructor, before AudioAppComponent's
    // own destructor runs, or the audio callback can fire into a partially
    // destroyed object.
    shutdownAudio();
}

void MainComponent::prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRate)
{
    voice.prepare (sampleRate);

    // T8/S9: the arp's and the sequencer's pending gates are both a count of
    // samples, so they are meaningless at a new rate. prepare() resets each
    // one along with its own clock (and, for the arp, the walker).
    arp.prepare (sampleRate);
    sequencer.prepare (sampleRate);

    // A device change means a stale delay-line read position and LFO phase
    // are meaningless at the new rate, same reasoning as arp/sequencer above.
    chorus.prepare (sampleRate);
    characterProcessor.prepare (sampleRate);

    // Force the first block after a device change to re-run the hand-over,
    // whichever side happens to be switched on.
    voiceOwner = VoiceOwner::Keys;
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Denormals in the filter's integrator states cost hundreds of cycles per
    // sample once the voice decays toward silence - flush them to zero.
    const juce::ScopedNoDenormals noDenormals;

    auto* buffer = bufferToFill.buffer;
    const auto numSamples = bufferToFill.numSamples;
    const auto startSample = bufferToFill.startSample;

    if (buffer->getNumChannels() == 0)
        return;

    // Mono voice: render once into channel 0, then fan out.
    auto* mono = buffer->getWritePointer (0, startSample);

    // The hand-over between the router, the arp and the sequencer, the event
    // drain and the render all live in renderVoiceBlock - a free function
    // rather than lines here, purely so runArpTransitionSelfTest and
    // runSeqTransitionSelfTest can drive the real thing rather than a copy of
    // it. Nothing moved out of this class's ownership: the voice, the router,
    // the arp, the sequencer and voiceOwner are all still members, passed in
    // by reference. See documents/arpeggiator-design.md section 7 and
    // documents/step-sequencer-design.md section 6.
    renderVoiceBlock (voice, router, arp, sequencer, voiceOwner, mono, numSamples);

    // character-and-vim.md B4, Tier 1 "Vim" - noise floor and output
    // saturation/asymmetric clipping, applied to the mono mix BEFORE chorus
    // (real analogue signal order: glue/saturation first, stereo widening
    // after). setEnabled mirrors SynthVoice's own once-per-block vimEnabled
    // read; off (the default) makes processSample an exact pass-through, so
    // this loop is a no-op read-then-write-the-same-value in that case.
    characterProcessor.setEnabled (voice.getParameters().vimEnabled.load (std::memory_order_relaxed) != 0);
    for (int i = 0; i < numSamples; ++i)
        mono[i] = characterProcessor.processSample (mono[i]);

    // character-and-vim.md B1: with chorus on and a real stereo output to
    // write into, REPLACE the plain mono-copied-to-both-channels fan-out
    // with the ensemble's own independently-modulated L/R pair, rather than
    // mixing it in afterwards - the doc's "stereo width" framing is about
    // what the two channels ARE, not an added effect layer. Off (the
    // default) leaves this whole block dead code and the fan-out below
    // exactly as it always was.
    if (voice.getParameters().chorusEnabled.load (std::memory_order_relaxed) != 0
        && buffer->getNumChannels() >= 2)
    {
        auto* left = mono; // channel 0's own write pointer - writing in place is safe,
                            // each iteration reads mono[i] before overwriting it.
        auto* right = buffer->getWritePointer (1, startSample);

        for (int i = 0; i < numSamples; ++i)
        {
            float outLeft = 0.0f, outRight = 0.0f;
            chorus.processSample (mono[i], outLeft, outRight);
            left[i] = outLeft;
            right[i] = outRight;
        }

        for (int channel = 2; channel < buffer->getNumChannels(); ++channel)
            buffer->copyFrom (channel, startSample, left, numSamples);

        return;
    }

    for (int channel = 1; channel < buffer->getNumChannels(); ++channel)
        buffer->copyFrom (channel, startSample, mono, numSamples);
}

void MainComponent::releaseResources()
{
    // Ordered voice, router, arp, seq: the voice goes silent first, then no
    // owner is left believing it is driving something. T7/S8 - without the
    // arp/seq reset, a restart would resurrect an open gate mid-step.
    voice.reset();

    // Clear held notes too, so stopping the device can't leave a phantom note
    // latched in the stack and stick on when it restarts.
    router.reset();
    arp.reset();
    sequencer.reset();

    // Hygiene, same reasoning as the three resets above: a restart should not
    // pick up whatever was left sitting in the delay line from before the
    // device stopped.
    chorus.reset();
    characterProcessor.reset();
}

void MainComponent::paint (juce::Graphics& g)
{
    // The letterbox bars either side of the scaled panel - SynthPanel paints
    // its own ground (PanelLookAndFeel::panel) inside itself, so this is only
    // ever visible when the window's aspect doesn't match the design canvas.
    g.fillAll (PanelLookAndFeel::background);
}

void MainComponent::resized()
{
    // documents/ui-design.md section 5, "The scaling, in
    // MainComponent::resized()" - fit-to-window scale, transform on the
    // CHILD panel rather than this component, so JUCE's hit-testing keeps
    // mapping mouse clicks through the transform correctly.
    const auto scale = juce::jmin ((float) getWidth()  / (float) SynthPanel::designWidth,
                                    (float) getHeight() / (float) SynthPanel::designHeight);

    // Centred, not just top-left anchored: the design canvas's 1.94:1 aspect
    // (documents/ui-design.md section 3) won't match every window shape, so
    // one axis is left with slack - split evenly either side rather than
    // dumped on the right/bottom.
    const auto scaledWidth  = (float) SynthPanel::designWidth  * scale;
    const auto scaledHeight = (float) SynthPanel::designHeight * scale;
    const auto offsetX = ((float) getWidth()  - scaledWidth)  * 0.5f;
    const auto offsetY = ((float) getHeight() - scaledHeight) * 0.5f;

    panel.setTransform (juce::AffineTransform::scale (scale).translated (offsetX, offsetY));
    panel.setBounds (0, 0, SynthPanel::designWidth, SynthPanel::designHeight); // pre-transform bounds
}
