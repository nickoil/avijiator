#include "MainComponent.h"

#include <cmath>

//==============================================================================
// Ordered top-to-bottom as they appear in the window. Item 2's nine controls,
// then item 3's four ADSR sliders and three LFO/depth sliders, landed
// incrementally by extending this table and bumping numDebugControls - the
// pattern item 4 will follow too.
const MainComponent::DebugControlSpec MainComponent::debugControlSpecs[numDebugControls] =
{
    { "Pitch",       20.0, 2000.0, 87.31, true,  &VoiceParameters::pitchLog2Hz },
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
};

namespace
{
    const char* const envelopeDestinationChoices[] = { "Filter", "Amp", "Both" };
    const char* const lfoWaveformChoices[] = { "Triangle", "Square", "S & H" };
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
};

MainComponent::MainComponent()
{
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

    gateButton.setButtonText ("Gate");
    gateButton.onGateChanged = [this] (bool isDown)
    {
        // Message thread: always safe to write an atomic. SynthVoice does its
        // own edge detection on the audio thread rather than being called
        // into directly here - see documents/envelope-lfo-design.md section 2.
        voice.getParameters().gate.store (isDown, std::memory_order_relaxed);
    };
    addAndMakeVisible (gateButton);

    // Sized for item 3's full control set (11 more rows land across steps
    // 1-4) so the window isn't resized again each step.
    setSize (640, 720);
    setAudioChannels (0, 2); // no input, stereo out
}

MainComponent::~MainComponent()
{
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
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawFittedText ("Item 3 complete: ADSR + LFO. Try LFO Waveform = S&H on Cutoff for a "
                      "stepped, not smooth, wobble.",
                       getLocalBounds().removeFromTop (60).reduced (20),
                       juce::Justification::centred,
                       2);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (60); // banner text

    // 120, not 90: item 3 adds combo-box rows with longer labels ("Env
    // Destination", "LFO Waveform") in later steps. Widened now so this
    // isn't revisited per-step; harmless for the existing shorter labels.
    static constexpr int labelWidth = 120;

    for (auto& control : debugControls)
    {
        auto row = area.removeFromTop (24);
        control.label.setBounds (row.removeFromLeft (labelWidth));
        control.slider.setBounds (row);

        area.removeFromTop (6); // gap between rows
    }

    for (auto& control : debugChoiceControls)
    {
        auto row = area.removeFromTop (24);
        control.label.setBounds (row.removeFromLeft (labelWidth));
        control.comboBox.setBounds (row);

        area.removeFromTop (6); // gap between rows
    }

    gateButton.setBounds (area.removeFromTop (24).removeFromLeft (labelWidth));
}
