#include "MainComponent.h"

MainComponent::MainComponent()
{
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
    currentSampleRate = sampleRate;
    phase = 0.0;
    phaseIncrement = juce::MathConstants<double>::twoPi * testToneFrequencyHz / currentSampleRate;
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Audio thread: no allocation, no locks, no logging — see CLAUDE.md.
    for (int sample = 0; sample < bufferToFill.numSamples; ++sample)
    {
        const auto value = (float) std::sin (phase) * testToneGain;

        for (int channel = 0; channel < bufferToFill.buffer->getNumChannels(); ++channel)
            bufferToFill.buffer->setSample (channel, bufferToFill.startSample + sample, value);

        phase += phaseIncrement;
        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;
    }
}

void MainComponent::releaseResources()
{
    // Nothing allocated yet — no-op until real DSP state shows up here.
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawFittedText ("Environment check: 440 Hz test tone playing.",
                       getLocalBounds().reduced (20),
                       juce::Justification::centred,
                       2);
}

void MainComponent::resized()
{
    // No child components yet.
}
