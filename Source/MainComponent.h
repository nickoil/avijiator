#pragma once

#include <array>
#include <functional>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "DSP/SynthVoice.h"

//==============================================================================
/*
    Top-level content component.

    Item 2 (oscillator + filter core) complete: four sources -> 24dB resonant
    lowpass -> VCA, nine debug controls. This scaffolding is explicitly
    throwaway - item 6 is the real UI pass and none of it survives that.
    See documents/dsp-voice-design.md for how it was built.

    Item 3 (envelope + LFO) complete: shared ADSR (routable VCF/VCA/Both) plus
    a triangle/square/S&H LFO (routable pitch and/or cutoff, independently),
    17 sliders + 2 combo boxes + Gate. This scaffolding is explicitly
    throwaway - item 6 is the real UI pass. See
    documents/envelope-lfo-design.md for how it was built.
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
    static constexpr int numDebugControls = 17;
    static const DebugControlSpec debugControlSpecs[numDebugControls];

    // Discrete switches (Envelope Destination, later LFO Waveform) don't fit
    // the float-slider table above - a small parallel table rather than
    // complicating that one with a variant type.
    struct DebugChoice
    {
        juce::ComboBox comboBox;
        juce::Label label;
    };

    struct DebugChoiceSpec
    {
        const char* name;
        const char* const* choices;
        int numChoices;
        int defaultIndex;
        std::atomic<int> VoiceParameters::* target;
    };

    static constexpr int numDebugChoiceControls = 2;
    static const DebugChoiceSpec debugChoiceSpecs[numDebugChoiceControls];

    // Item 3 step 1 (Gate): a temporary manual trigger for testing the
    // envelope before real note input exists (item 4). juce::Button's
    // onStateChange lambda would be the shorter way to get press/release, but
    // its exact behaviour isn't something to assume without reading JUCE
    // sources - overriding mouseDown/mouseUp is unambiguous, since
    // Component's mouse-capture behaviour (mouseUp still reaches the
    // component that started the drag, even outside its bounds) is
    // foundational and certain.
    struct GateButton final : public juce::TextButton
    {
        std::function<void (bool)> onGateChanged;

        void mouseDown (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseDown (e);
            if (onGateChanged != nullptr)
                onGateChanged (true);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseUp (e);
            if (onGateChanged != nullptr)
                onGateChanged (false);
        }
    };

    SynthVoice voice;
    std::array<DebugControl, numDebugControls> debugControls;
    std::array<DebugChoice, numDebugChoiceControls> debugChoiceControls;
    GateButton gateButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
