#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

//==============================================================================
/*
    Top-level content component. Empty placeholder for now — audio device
    setup and sound generation land in Stage A item 1 (environment check).
*/
class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
