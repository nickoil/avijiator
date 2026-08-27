#include "MainComponent.h"

#include <cmath>

//==============================================================================
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
    // pushNoteEvent closes over router, which must already be constructed -
    // see the member comment in MainComponent.h. It is only ever CALLED
    // later, from the message thread, never during construction.
    : panel (voice.getParameters(), [this] (const NoteEvent& event) { router.pushUiEvent (event); })
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
   #endif

    qwertyInput.onNoteEvent = [this] (const NoteEvent& event)
    {
        router.pushUiEvent (event);
    };

    panel.onAudioSettingsClicked = [this] { showAudioSettings(); };

    // Octave buttons drive QwertyNoteInput's own shift (comma/period's exact
    // same clamped adjustment) and then mirror the result back into the
    // panel, the same way keyStateChanged does below for comma/period - see
    // SynthPanel::onOctaveUpClicked's comment.
    panel.onOctaveUpClicked = [this]
    {
        qwertyInput.octaveUp();
        panel.setOctaveShift (qwertyInput.getOctaveShift());
    };
    panel.onOctaveDownClicked = [this]
    {
        qwertyInput.octaveDown();
        panel.setOctaveShift (qwertyInput.getOctaveShift());
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

    // Mirrors the shift into the panel so a comma/period press moves the
    // on-screen keyboard and its readout exactly as an Octave button click
    // would - see SynthPanel::onOctaveUpClicked's comment.
    panel.setOctaveShift (qwertyInput.getOctaveShift());

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

    // T8: the arp's pending gate is a count of samples, so it is meaningless
    // at a new rate. prepare() resets it along with the clock and the walker.
    arp.prepare (sampleRate);

    // Force the first block after a device change to re-run the hand-over,
    // whichever side happens to be switched on.
    arpWasOn = false;
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

    // The hand-over between the router and the arp, the event drain and the
    // render all live in renderVoiceBlock - a free function rather than lines
    // here, purely so runArpTransitionSelfTest can drive the real thing rather
    // than a copy of it. Nothing moved out of this class's ownership: the
    // voice, the router, the arp and arpWasOn are all still members, passed in
    // by reference. See documents/arpeggiator-design.md section 7.
    renderVoiceBlock (voice, router, arp, arpWasOn, mono, numSamples);

    for (int channel = 1; channel < buffer->getNumChannels(); ++channel)
        buffer->copyFrom (channel, startSample, mono, numSamples);
}

void MainComponent::releaseResources()
{
    // Ordered voice, router, arp: the voice goes silent first, then neither
    // owner is left believing it is driving something. T7 - without the arp
    // reset, a restart would resurrect an open gate mid-step.
    voice.reset();

    // Clear held notes too, so stopping the device can't leave a phantom note
    // latched in the stack and stick on when it restarts.
    router.reset();
    arp.reset();
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
