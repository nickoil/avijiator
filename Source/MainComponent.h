#pragma once

#include <array>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "DSP/SynthVoice.h"

//==============================================================================
/*
    Top-level content component.

    Item 2 step 6 (Resonance): the complete item 2 voice - four sources ->
    24dB resonant lowpass -> VCA, nine controls.
    See documents/dsp-voice-design.md for the full build order.
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
    //==============================================================================
    // Throwaway auditioning scaffolding. Item 6 is the real UI pass and none of
    // this survives it - deliberately unstyled, and driven by one spec table
    // plus one loop so later steps add a row rather than more copy-paste.
    struct DebugControl
    {
        juce::Slider slider;
        juce::Label label;
    };

    struct DebugControlSpec
    {
        const char* name;
        double minimum, maximum, defaultValue;
        bool storeAsLog2;                             // true for Hz-valued controls
        std::atomic<float> VoiceParameters::* target;
    };

    // Sizing the definition to this count makes the compiler enforce that the
    // table and the array stay in step.
    static constexpr int numDebugControls = 9;
    static const DebugControlSpec debugControlSpecs[numDebugControls];

    SynthVoice voice;
    std::array<DebugControl, numDebugControls> debugControls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
