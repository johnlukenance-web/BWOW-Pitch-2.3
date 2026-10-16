#pragma once
#include "PluginProcessor.h"

class BwowPitch2Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BwowPitch2Editor (BwowPitch2Processor&);
    ~BwowPitch2Editor() override = default;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    BwowPitch2Processor& proc;
    juce::Slider amount, decay, sens;
    juce::Label amountL, decayL, sensL;
    juce::AudioProcessorValueTreeState::SliderAttachment amountA, decayA, sensA;
    int lastCount = 0;
    float led = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BwowPitch2Editor)
};
