#include "MainComponent.h"

MainComponent::MainComponent()
{
    setSize (600, 400);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    // Child component layout goes here once there are controls to lay out.
}
