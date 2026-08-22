#include "MainComponent.h"

MainComponent::MainComponent()
{
    levelLabel.setText ("Level", juce::dontSendNotification);
    levelLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (levelLabel);

    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    levelSlider.setRange (0.0, 1.0);
    levelSlider.setValue (0.25, juce::dontSendNotification);
    levelSlider.onValueChange = [this]
    {
        voice.getParameters().outputLevel.store ((float) levelSlider.getValue(), std::memory_order_relaxed);
    };
    addAndMakeVisible (levelSlider);

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
    // Denormals in filter integrator states (from item 2 step 5 onward) cost
    // hundreds of cycles per sample once the voice decays toward silence -
    // flush them to zero. Required from the start, not added later.
    const juce::ScopedNoDenormals noDenormals;

    auto* buffer = bufferToFill.buffer;
    const auto numSamples = bufferToFill.numSamples;
    const auto startSample = bufferToFill.startSample;

    if (buffer->getNumChannels() == 0)
        return;

    // Mono voice: render once into channel 0, then fan out. Item 5 splits
    // this single call into per-step sub-blocks - the signature already
    // allows it.
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
    g.drawFittedText ("Item 2 step 1: plumbing proven. Naive saw through a working Level control.",
                       getLocalBounds().removeFromTop (60).reduced (20),
                       juce::Justification::centred,
                       2);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (60); // banner text

    auto row = area.removeFromTop (24);
    levelLabel.setBounds (row.removeFromLeft (80));
    levelSlider.setBounds (row);
}
