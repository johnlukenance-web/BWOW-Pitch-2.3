#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout BwowPitch2Processor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "amount", 1 }, "Amount", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 65.0f,
        AudioParameterFloatAttributes().withLabel ("cents")));

    NormalisableRange<float> decayRange (20.0f, 1000.0f, 1.0f);
    decayRange.setSkewForCentre (200.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "decay", 1 }, "Decay", decayRange, 143.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "sens", 1 }, "Trigger Sensitivity", 0, 10, 0,
        AudioParameterIntAttributes()
            .withStringFromValueFunction ([] (int v, int) { return v == 0 ? String ("Auto") : String (v); })
            .withValueFromStringFunction ([] (const String& t) { return t.trim().equalsIgnoreCase ("auto") ? 0 : t.getIntValue(); })));
    return layout;
}

BwowPitch2Processor::BwowPitch2Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    amountParam = apvts.getRawParameterValue ("amount");
    decayParam = apvts.getRawParameterValue ("decay");
    sensParam = apvts.getRawParameterValue ("sens");
}

bool BwowPitch2Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BwowPitch2Processor::prepareToPlay (double sampleRate, int)
{
    core.prepare (sampleRate, juce::jmax (1, getTotalNumInputChannels()));
    setLatencySamples (core.getLatencySamples()); // reported to the host (REAPER applies PDC)
}

void BwowPitch2Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    core.setParameters (amountParam->load(), decayParam->load(), (int) std::lround (sensParam->load()));
    core.process (buffer.getArrayOfWritePointers(), numCh, buffer.getNumSamples());
}

juce::AudioProcessorEditor* BwowPitch2Processor::createEditor() { return new BwowPitch2Editor (*this); }

void BwowPitch2Processor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void BwowPitch2Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BwowPitch2Processor(); }
