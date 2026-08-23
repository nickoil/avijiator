#include "MainComponent.h"

#include <cmath>

//==============================================================================
// Ordered top-to-bottom as they appear in the window. Later build steps insert
// rows here (Pulse, Pulse width, Sub, Noise, Cutoff, Resonance) and bump
// numDebugControls to match.
const MainComponent::DebugControlSpec MainComponent::debugControlSpecs[numDebugControls] =
{
    { "Pitch",       20.0, 2000.0, 87.31, true,  &VoiceParameters::pitchLog2Hz },
    { "Saw",          0.0,    1.0,  0.70, false, &VoiceParameters::sawLevel    },
    { "Pulse",        0.0,    1.0,  0.00, false, &VoiceParameters::pulseLevel  },
    { "Pulse width",  0.02,   0.98, 0.50, false, &VoiceParameters::pulseWidth  },
    { "Level",        0.0,    1.0,  0.25, false, &VoiceParameters::outputLevel },
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

    setSize (600, 400);
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
    // Denormals in filter integrator states (from step 5 onward) cost hundreds
    // of cycles per sample once the voice decays toward silence - flush them
    // to zero.
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
    g.drawFittedText ("Item 2 step 3 (Pulse): saw + pulse with PWM. Turn Saw down and Pulse up, "
                      "then sweep Pulse width.",
                       getLocalBounds().removeFromTop (60).reduced (20),
                       juce::Justification::centred,
                       2);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (60); // banner text

    for (auto& control : debugControls)
    {
        auto row = area.removeFromTop (24);
        control.label.setBounds (row.removeFromLeft (90));
        control.slider.setBounds (row);

        area.removeFromTop (6); // gap between rows
    }
}
