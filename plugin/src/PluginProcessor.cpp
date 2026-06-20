// plugin/src/PluginProcessor.cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "pitch.h"  // kVoctZero

SawstackAudioProcessor::SawstackAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout
SawstackAudioProcessor::createLayout() {
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<P>("detune", "Detune", R{0.0f, 1.0f}, 0.3f));
    params.push_back(std::make_unique<P>("morph",  "Morph",  R{0.0f, 1.0f}, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode", juce::StringArray{"Stack", "Rich", "Sub"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "width", "Width", juce::StringArray{"Mono", "Stereo", "Wide"}, 1));
    params.push_back(std::make_unique<P>("attack",  "Attack",  R{0.001f, 10.0f, 0.0f, 0.3f}, 0.005f));
    params.push_back(std::make_unique<P>("decay",   "Decay",   R{0.001f, 10.0f, 0.0f, 0.3f}, 0.2f));
    params.push_back(std::make_unique<P>("sustain", "Sustain", R{0.0f, 1.0f}, 0.8f));
    params.push_back(std::make_unique<P>("release", "Release", R{0.001f, 10.0f, 0.0f, 0.3f}, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterInt>("coarse", "Coarse", -24, 24, 0));
    params.push_back(std::make_unique<P>("fine", "Fine", R{-100.0f, 100.0f}, 0.0f));
    params.push_back(std::make_unique<P>("level", "Level", R{-60.0f, 6.0f}, 0.0f));

    return { params.begin(), params.end() };
}

void SawstackAudioProcessor::prepareToPlay(double sampleRate, int) {
    engine_.Init(static_cast<float>(sampleRate));
}

void SawstackAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&) {
    const int n = buffer.getNumSamples();

    sawstack::Params p{};
    p.top_adc    = apvts.getRawParameterValue("detune")->load();
    p.bottom_adc = apvts.getRawParameterValue("morph")->load();
    p.voct_adc   = sawstack::kVoctZero;
    p.mode  = static_cast<sawstack::Mode>(
        static_cast<int>(apvts.getRawParameterValue("mode")->load()));
    p.width = static_cast<sawstack::Width>(
        static_cast<int>(apvts.getRawParameterValue("width")->load()));
    p.external_hz = 261.63f;  // fixed drone until Task 6
    engine_.ApplyParams(p);

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : l;
    engine_.ProcessBlock(l, r, n);

    const float gainDb = apvts.getRawParameterValue("level")->load();
    buffer.applyGain(juce::Decibels::decibelsToGain(gainDb));
}

juce::AudioProcessorEditor* SawstackAudioProcessor::createEditor() {
    return new SawstackAudioProcessorEditor(*this);
}

void SawstackAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void SawstackAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SawstackAudioProcessor();
}
