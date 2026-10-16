#include "PluginEditor.h"

BwowPitch2Editor::BwowPitch2Editor (BwowPitch2Processor& p)
    : AudioProcessorEditor (&p), proc (p),
      amountA (p.apvts, "amount", amount), decayA (p.apvts, "decay", decay), sensA (p.apvts, "sens", sens)
{
    auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& text, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 20);
        if (suffix.isNotEmpty()) s.setTextValueSuffix (suffix);
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (s); addAndMakeVisible (l);
    };
    setup (amount, amountL, "Amount", " cents");
    setup (decay, decayL, "Decay", " ms");
    setup (sens, sensL, "Trigger Sensitivity", "");
    setSize (360, 150);
    lastCount = proc.getTriggerCount();
    startTimerHz (30);
}

void BwowPitch2Editor::timerCallback()
{
    const int c = proc.getTriggerCount();
    if (c != lastCount) { lastCount = c; led = 1.0f; }
    else led *= 0.82f;
    repaint (getWidth() - 40, 4, 32, 24);
}

void BwowPitch2Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1c20));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Bwow Pitch 2", 12, 6, 200, 22, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff3a3a40));
    g.fillEllipse ((float) getWidth() - 32.0f, 9.0f, 14.0f, 14.0f);
    g.setColour (juce::Colour (0xffff7a1a).withAlpha (led));
    g.fillEllipse ((float) getWidth() - 32.0f, 9.0f, 14.0f, 14.0f);
}

void BwowPitch2Editor::resized()
{
    auto r = getLocalBounds().reduced (12).withTrimmedTop (26);
    const int rowH = r.getHeight() / 3;
    for (auto pair : { std::pair<juce::Label*, juce::Slider*> { &amountL, &amount }, { &decayL, &decay }, { &sensL, &sens } })
    {
        auto row = r.removeFromTop (rowH);
        pair.first->setBounds (row.removeFromLeft (120));
        pair.second->setBounds (row);
    }
}
