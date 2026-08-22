#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "DSP/SynthVoice.h"

//==============================================================================
/*
    Top-level content component.

    Item 2 step 1 (plumbing): renders SynthVoice's placeholder naive saw
    through a working Level control. No PolyBLEP oscillator, no filter yet -
    see documents/dsp-voice-design.md for the full build order.
*/
class MainComponent final : public juce::AudioAppComponent
{
public:
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SynthVoice voice;

    juce::Slider levelSlider;
    juce::Label levelLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
