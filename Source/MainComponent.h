#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

//==============================================================================
/*
    Top-level content component.

    Stage A item 1 (environment check): plays a fixed 440Hz sine tone to
    confirm the audio device path works end-to-end. This is deliberately
    NOT the real oscillator — that's item 2 (PolyBLEP saw/square + sub +
    noise -> SVF), which is DSP work done under plan mode.
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
    static constexpr double testToneFrequencyHz = 440.0;
    static constexpr float testToneGain = 0.15f;

    double currentSampleRate = 0.0;
    double phase = 0.0;
    double phaseIncrement = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
