#include "MainComponent.h"

#include <cmath>

//==============================================================================
// Ordered top-to-bottom as they appear in the window. Item 2's nine controls,
// then item 3's four ADSR sliders and three LFO/depth sliders, landed
// incrementally by extending this table and bumping numDebugControls - the
// pattern item 4 will follow too.
const MainComponent::DebugControlSpec MainComponent::debugControlSpecs[numDebugControls] =
{
    { "Saw",          0.0,    1.0,  0.70, false, &VoiceParameters::sawLevel    },
    { "Pulse",        0.0,    1.0,  0.00, false, &VoiceParameters::pulseLevel  },
    { "Pulse width",  0.02,   0.98, 0.50, false, &VoiceParameters::pulseWidth  },
    { "Sub",          0.0,    1.0,  0.00, false, &VoiceParameters::subLevel    },
    { "Noise",        0.0,    1.0,  0.00, false, &VoiceParameters::noiseLevel  },
    { "Cutoff",      20.0, 18000.0, 2000.0, true, &VoiceParameters::cutoffLog2Hz },
    { "Resonance",    0.0,    1.0,  0.20, false, &VoiceParameters::resonance   },
    { "Level",        0.0,    1.0,  0.25, false, &VoiceParameters::outputLevel },
    { "Attack",     0.001,    5.0,  0.01, false, &VoiceParameters::attackSeconds  },
    { "Decay",      0.001,    5.0,   0.1, false, &VoiceParameters::decaySeconds   },
    { "Sustain",       0.0,    1.0,   0.7, false, &VoiceParameters::sustainLevel  },
    { "Release",    0.001,    5.0,   0.3, false, &VoiceParameters::releaseSeconds },
    { "Env->Cutoff",   0.0,    8.0,   0.0, false, &VoiceParameters::envToCutoffDepthOctaves },
    { "LFO Rate",     0.02,   20.0,   2.0, false, &VoiceParameters::lfoRateHz },
    { "LFO->Pitch",    0.0,    1.0,   0.0, false, &VoiceParameters::lfoToPitchDepthOctaves },
    { "LFO->Cutoff",   0.0,    8.0,   0.0, false, &VoiceParameters::lfoToCutoffDepthOctaves },
    { "Glide Time",    0.0,    5.0,   0.0, false, &VoiceParameters::glideTimeSeconds },
};

// A fifth apart, so a glide between them is unmistakable.
const MainComponent::NoteButtonSpec MainComponent::noteButtonSpecs[numNoteButtons] =
{
    { "Note C4", 60 },
    { "Note G4", 67 },
};

// One octave, C3 to C4 inclusive - the closing C makes it read as a keyboard
// rather than stopping awkwardly on B.
const MainComponent::KeyboardKeySpec MainComponent::keyboardKeySpecs[numKeyboardKeys] =
{
    { "C",   0, false }, { "C#",  1, true  }, { "D",   2, false }, { "D#",  3, true  },
    { "E",   4, false }, { "F",   5, false }, { "F#",  6, true  }, { "G",   7, false },
    { "G#",  8, true  }, { "A",   9, false }, { "A#", 10, true  }, { "B",  11, false },
    { "C'", 12, false },
};

namespace
{
    const char* const envelopeDestinationChoices[] = { "Filter", "Amp", "Both" };
    const char* const lfoWaveformChoices[] = { "Triangle", "Square", "S & H" };
    const char* const legatoRetriggerChoices[] = { "Retrigger", "Legato" };
    const char* const notePriorityChoices[] = { "Last Note", "Highest Note" };
}

// Parallel to debugControlSpecs above, for discrete switches a Slider can't
// represent. Index into `choices` matches the enum's underlying int value
// (EnvelopeDestination::Filter = 0, Amp = 1, Both = 2), so the combo box's
// selected index can be stored straight into the atomic<int> with no mapping.
const MainComponent::DebugChoiceSpec MainComponent::debugChoiceSpecs[numDebugChoiceControls] =
{
    {
        "Env Destination",
        envelopeDestinationChoices,
        (int) (sizeof (envelopeDestinationChoices) / sizeof (envelopeDestinationChoices[0])),
        (int) EnvelopeDestination::Amp,
        &VoiceParameters::envelopeDestination
    },
    {
        "LFO Waveform",
        lfoWaveformChoices,
        (int) (sizeof (lfoWaveformChoices) / sizeof (lfoWaveformChoices[0])),
        (int) Lfo::Waveform::Triangle,
        &VoiceParameters::lfoWaveform
    },
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

namespace
{
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
{
   #if JUCE_DEBUG
    // All three of these fail SILENTLY when wrong - a dropped note event, a
    // wrong fallback note that just sounds like a playing mistake, or a
    // note-on-velocity-0 misread as a note-on and stuck forever. Asserted at
    // startup rather than left to be noticed by ear much later.
    runNoteEventFifoSelfTest();
    runNoteStackSelfTest();
    runMidiConversionSelfTest();
   #endif

    for (int i = 0; i < numDebugControls; ++i)
    {
        const auto& spec = debugControlSpecs[i];
        auto& control = debugControls[(size_t) i];

        control.label.setText (spec.name, juce::dontSendNotification);
        control.label.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (control.label);

        control.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        control.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        control.slider.setRange (spec.minimum, spec.maximum);

        // Hz-valued controls get a log-ish taper so the useful low end isn't
        // crammed into the first few pixels of travel.
        if (spec.storeAsLog2)
            control.slider.setSkewFactorFromMidPoint (std::sqrt (spec.minimum * spec.maximum));

        control.slider.setValue (spec.defaultValue, juce::dontSendNotification);
        addAndMakeVisible (control.slider);

        control.slider.onValueChange = [this, &spec, &control]
        {
            const auto value = (float) control.slider.getValue();

            (voice.getParameters().*spec.target)
                .store (spec.storeAsLog2 ? std::log2 (value) : value, std::memory_order_relaxed);
        };

        // Seed the atomics from the table so the sliders and the voice cannot
        // disagree at startup - VoiceParameters' own defaults are a fallback.
        control.slider.onValueChange();
    }

    for (int i = 0; i < numDebugChoiceControls; ++i)
    {
        const auto& spec = debugChoiceSpecs[i];
        auto& control = debugChoiceControls[(size_t) i];

        control.label.setText (spec.name, juce::dontSendNotification);
        control.label.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (control.label);

        // juce::ComboBox item IDs are 1-based - 0 is reserved to mean "no
        // selection" - so store id = choice index + 1, and subtract 1 back
        // off when reading getSelectedId().
        for (int choice = 0; choice < spec.numChoices; ++choice)
            control.comboBox.addItem (spec.choices[choice], choice + 1);

        control.comboBox.setSelectedId (spec.defaultIndex + 1, juce::dontSendNotification);
        addAndMakeVisible (control.comboBox);

        control.comboBox.onChange = [this, &spec, &control]
        {
            const auto index = control.comboBox.getSelectedId() - 1;
            (voice.getParameters().*spec.target).store (index, std::memory_order_relaxed);
        };

        // Seed the atomic from the table, matching the float sliders' pattern.
        control.comboBox.onChange();
    }

    for (int i = 0; i < numNoteButtons; ++i)
    {
        const auto& spec = noteButtonSpecs[i];
        auto& button = noteButtons[(size_t) i];

        button.setButtonText (spec.name);
        button.onPressedChanged = [this, &spec, i] (bool isDown)
        {
            // LATCHING, not momentary. A mouse has exactly one pointer, so
            // momentary buttons could never be held down together - and
            // overlapping notes are precisely what glide and legato need in
            // order to be tested at all. Click toggles; the release is
            // ignored. QWERTY and MIDI (steps 5 and 7) are naturally
            // momentary and need none of this.
            if (! isDown)
                return;

            auto& latched = noteButtonLatched[(size_t) i];
            latched = ! latched;

            // Message thread, so constructing a juce::String here is fine -
            // CLAUDE.md's ban on that is audio-thread-scoped.
            noteButtons[(size_t) i].setButtonText (latched ? juce::String (spec.name) + " (on)"
                                                            : juce::String (spec.name));

            // Pitch is converted HERE, at the producer, so the audio thread
            // never computes a logarithm and every input source converts
            // identically.
            //
            // Never call voice.noteOn() directly from here: that would be an
            // unsynchronized write into audio-thread state, exactly what this
            // FIFO exists to prevent.
            router.pushUiEvent ({ latched ? NoteEvent::Type::NoteOn : NoteEvent::Type::NoteOff,
                                  (std::uint8_t) spec.midiNoteNumber,
                                  pitchLog2HzForMidiNote (spec.midiNoteNumber),
                                  1.0f });
        };
        addAndMakeVisible (button);
    }

    qwertyInput.onNoteEvent = [this] (const NoteEvent& event)
    {
        router.pushUiEvent (event);
    };

    for (int i = 0; i < numKeyboardKeys; ++i)
    {
        const auto& spec = keyboardKeySpecs[i];
        auto& key = keyboardButtons[(size_t) i];

        key.setButtonText (spec.name);
        key.setWantsKeyboardFocus (false);

        // Black keys darker, so the layout reads as a keyboard at a glance.
        key.setColour (juce::TextButton::buttonColourId,
                        spec.isBlackKey ? juce::Colours::darkslategrey : juce::Colours::grey);

        key.onPressedChanged = [this, &spec] (bool isDown)
        {
            const auto noteNumber = keyboardBaseNoteNumber + spec.semitoneOffset;

            router.pushUiEvent ({ isDown ? NoteEvent::Type::NoteOn : NoteEvent::Type::NoteOff,
                                  (std::uint8_t) noteNumber,
                                  pitchLog2HzForMidiNote (noteNumber),
                                  1.0f });
        };

        addAndMakeVisible (key);
    }

    audioSettingsButton.setButtonText ("Audio Settings");
    audioSettingsButton.onClick = [this] { showAudioSettings(); };
    audioSettingsButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (audioSettingsButton);

    // The component itself takes keyboard focus, and the mouse-driven
    // controls explicitly decline it - otherwise clicking a slider would
    // steal focus and silently stop the keyboard playing notes. Combo boxes
    // are left alone, since they genuinely need keys to operate.
    setWantsKeyboardFocus (true);

    for (auto& control : debugControls)
        control.slider.setWantsKeyboardFocus (false);

    for (auto& button : noteButtons)
        button.setWantsKeyboardFocus (false);

    // Two columns of controls - 21 rows in a single column needs ~700px of
    // height, which pushed the button row off the bottom of the window.
    setSize (900, 560);
    setAudioChannels (0, 2); // no input, stereo out

    // After setAudioChannels, so the device manager is initialised.
    enableAllMidiInputs();
}

void MainComponent::enableAllMidiInputs()
{
    // Enable every MIDI input that's present, rather than making the user go
    // and tick one before anything plays. For a synth, receiving from
    // whatever is plugged in is the sane default; the Audio Settings panel
    // can untick any that aren't wanted.
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        deviceManager.setMidiInputDeviceEnabled (device.identifier, true);

    // An empty identifier registers as a catch-all across every ENABLED
    // input, so devices switched on later in the settings panel are picked up
    // without re-registering per device.
    deviceManager.addMidiInputDeviceCallback ({}, this);
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

    // Not consumed: this only observes key state, so anything else that wants
    // these keys should still get them.
    return false;
}

void MainComponent::focusLost (FocusChangeType /*cause*/)
{
    qwertyInput.releaseAllHeldKeys();
}

MainComponent::~MainComponent()
{
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

    // Drain queued note events before rendering, so the envelope and glide
    // target are settled for the whole block. Block-granular rather than
    // sample-accurate is deliberate for item 4 - human timing jitter dwarfs a
    // block boundary, and CLAUDE.md ties sample-accuracy to item 5's arp
    // clock specifically.
    const auto priorityMode = (NotePriorityMode)
        voice.getParameters().notePriorityMode.load (std::memory_order_relaxed);

    router.dispatchPendingEvents (voice, priorityMode);

    // Mono voice: render once into channel 0, then fan out. Item 5 splits this
    // single call into per-step sub-blocks - the signature already allows it.
    auto* mono = buffer->getWritePointer (0, startSample);
    voice.renderNextBlock (mono, numSamples);

    for (int channel = 1; channel < buffer->getNumChannels(); ++channel)
        buffer->copyFrom (channel, startSample, mono, numSamples);
}

void MainComponent::releaseResources()
{
    voice.reset();

    // Clear held notes too, so stopping the device can't leave a phantom note
    // latched in the stack and stick on when it restarts.
    router.reset();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawFittedText ("Play: MIDI keyboard, computer keys Z S X D C V G B H N J M (comma/period "
                      "shift octave, click background first), or the keyboard below.",
                       getLocalBounds().removeFromTop (60).reduced (20),
                       juce::Justification::centred,
                       2);
}

void MainComponent::resized()
{
    static constexpr int labelWidth = 120;
    static constexpr int rowHeight = 24;
    static constexpr int rowGap = 6;

    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (60); // banner text

    // Buttons come off the BOTTOM first, so however many control rows there
    // are they can never push the buttons off-screen - which is exactly what
    // was happening with a single column.
    auto buttonRow = area.removeFromBottom (28);
    area.removeFromBottom (12);

    for (auto& button : noteButtons)
    {
        button.setBounds (buttonRow.removeFromLeft (110));
        buttonRow.removeFromLeft (8);
    }

    buttonRow.removeFromLeft (24); // separate the settings button from the notes
    audioSettingsButton.setBounds (buttonRow.removeFromLeft (130));

    // Clickable keyboard sits just above the buttons, also reserved from the
    // bottom so control rows can never push it off-screen.
    area.removeFromBottom (10);
    auto keyboardRow = area.removeFromBottom (40);
    area.removeFromBottom (12);

    const auto keyWidth = keyboardRow.getWidth() / numKeyboardKeys;

    for (auto& key : keyboardButtons)
        key.setBounds (keyboardRow.removeFromLeft (keyWidth).reduced (1, 0));

    //==========================================================================
    // Two columns, filled left-then-right.
    const auto totalRows = numDebugControls + numDebugChoiceControls;
    const auto rowsInLeftColumn = (totalRows + 1) / 2;

    auto leftColumn = area.removeFromLeft (area.getWidth() / 2 - 12);
    area.removeFromLeft (24); // gutter
    auto rightColumn = area;

    auto rowIndex = 0;

    const auto nextRow = [&] () -> juce::Rectangle<int>
    {
        auto& column = rowIndex++ < rowsInLeftColumn ? leftColumn : rightColumn;
        auto row = column.removeFromTop (rowHeight);
        column.removeFromTop (rowGap);
        return row;
    };

    for (auto& control : debugControls)
    {
        auto row = nextRow();
        control.label.setBounds (row.removeFromLeft (labelWidth));
        control.slider.setBounds (row);
    }

    for (auto& control : debugChoiceControls)
    {
        auto row = nextRow();
        control.label.setBounds (row.removeFromLeft (labelWidth));
        control.comboBox.setBounds (row);
    }
}
